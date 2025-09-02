#ifdef _WIN32
#include <ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#endif

#include "internal/tftp_server_impl.h"
#include "tftp/tftp_packet.h"
#include "tftp/tftp_logger.h"
#include "tftp/tftp_util.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <cerrno>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#include <process.h>
#define CLOSESOCKET closesocket
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define CLOSESOCKET close
#endif

namespace tftpserver {
namespace internal {

constexpr int kMaxRetries = 5;
constexpr int kRetryTimeoutMs = 1000;
// kMaxPacketSize and kMaxDataSize are already defined in tftp_common.h, so not redefined here

TftpServerImpl::TftpServerImpl(const std::string& root_dir, uint16_t port)
    : root_dir_(root_dir),
      port_(port),
#ifdef _WIN32
      sockfd_(INVALID_SOCKET),
#else
      sockfd_(-1),
#endif
      running_(false),
      thread_pool_(nullptr),
      secure_mode_(true),
      max_transfer_size_(1024 * 1024 * 1024),  // 1MB
      timeout_seconds_(5),
      thread_pool_size_(std::thread::hardware_concurrency()) {
    if (!root_dir_.empty() && root_dir_.back() != '/' && root_dir_.back() != '\\') {
        root_dir_ += '/';
    }
    
    // Set default callbacks
    read_callback_ = TftpServerImpl::DefaultReadHandler;
    write_callback_ = TftpServerImpl::DefaultWriteHandler;
}

TftpServerImpl::~TftpServerImpl() {
    Stop();
}

bool TftpServerImpl::Start() {
    if (running_) {
        TFTP_INFO("TFTP server is already running");
        return true;
    }
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        TFTP_ERROR("WSAStartup failed");
        return false;
    }
#endif
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (sockfd_ == INVALID_SOCKET) {
#else
    if (sockfd_ < 0) {
#endif
        TFTP_ERROR("UDP socket creation failed");
        return false;
    }
    int opt = 1;
#ifdef _WIN32
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
#else
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
#endif
        TFTP_ERROR("Socket option setting failed");
        CLOSESOCKET(sockfd_);
        return false;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    if (bind(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        TFTP_ERROR("UDP socket bind failed");
        CLOSESOCKET(sockfd_);
        return false;
    }
    // Initialize thread pool with mutex protection
    {
        std::lock_guard<std::mutex> lock(thread_pool_mutex_);
        thread_pool_ = std::make_unique<TftpThreadPool>(thread_pool_size_);
    }
    
    running_ = true;
    server_thread_ = std::thread(&TftpServerImpl::ServerLoop, this);
    TFTP_INFO("TFTP server started on port %d with %zu worker threads", port_, thread_pool_size_);
    return true;
}

void TftpServerImpl::Stop() {
    if (!running_) return;
    
    // Set flag first so ServerLoop exits early
    running_ = false;
    
    // Shutdown thread pool first to stop accepting new tasks
    {
        std::lock_guard<std::mutex> lock(thread_pool_mutex_);
        if (thread_pool_) {
            thread_pool_->Shutdown();
            thread_pool_.reset();
        }
    }
    
    // Close socket
    if (
#ifdef _WIN32
        sockfd_ != INVALID_SOCKET
#else
        sockfd_ >= 0
#endif
    ) {
        CLOSESOCKET(sockfd_);
#ifdef _WIN32
        sockfd_ = INVALID_SOCKET;
#else
        sockfd_ = -1;
#endif
    }
    
    // Wait for thread to finish
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    TFTP_INFO("TFTP server stopped");
}

bool TftpServerImpl::IsRunning() const {
    // Check if server is running with multiple conditions
    if (!running_) {
        // If running flag is false, server is stopped
        return false;
    }
    
    // Also check if socket is valid
#ifdef _WIN32
    if (sockfd_ == INVALID_SOCKET) {
#else
    if (sockfd_ < 0) {
#endif
        // If socket is invalid, server is not running
        return false;
    }
    
    // Also check if thread is joinable
    if (!server_thread_.joinable()) {
        // If thread is not joinable, server is not running
        return false;
    }
    
    // If all conditions are met, server is running
    return true;
}

void TftpServerImpl::ServerLoop() {
    while (running_) {
        uint8_t buffer[kMaxPacketSize];
        sockaddr_in client_addr = {};
#ifdef _WIN32
        int addrlen = sizeof(client_addr);
#else
        socklen_t addrlen = sizeof(client_addr);
#endif
        int recvlen = recvfrom(sockfd_, (char*)buffer, sizeof(buffer), 0,
                             (struct sockaddr*)&client_addr, &addrlen);
        if (recvlen > 0) {
            TFTP_INFO("Received packet from client: %zu bytes", static_cast<size_t>(recvlen));
            // Submit client handling to thread pool with proper synchronization
            {
                std::lock_guard<std::mutex> lock(thread_pool_mutex_);
                if (thread_pool_ && !thread_pool_->IsShuttingDown()) {
                    try {
                        // Create vector once and move it to avoid copy
                        std::vector<uint8_t> packet_data;
                        packet_data.reserve(recvlen);
                        packet_data.assign(buffer, buffer + recvlen);
                        
                        thread_pool_->Submit([this, packet_data = std::move(packet_data), client_addr]() {
                            this->HandleClient(packet_data, client_addr);
                        });
                    } catch (const std::exception& e) {
                        TFTP_ERROR("Failed to submit client task to thread pool: %s", e.what());
                    }
                } else {
                    TFTP_WARN("Thread pool not available, dropping client request");
                }
            }
        }
    }
}

void TftpServerImpl::HandleClient(const std::vector<uint8_t>& initial_packet,
                                   const sockaddr_in& client_addr) {
    try {
        TFTP_INFO("HandleClient called with packet size: %zu", initial_packet.size());
        
        // Debug: dump packet contents in hex
        std::string hex_dump;
        for (size_t i = 0; i < std::min(initial_packet.size(), size_t(100)); ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", initial_packet[i]);
            hex_dump += buf;
        }
        TFTP_INFO("Packet hex dump (first 100 bytes): %s", hex_dump.c_str());
        
        TftpPacket packet;
        if (!packet.Deserialize(initial_packet)) {
            TFTP_ERROR("Invalid packet received");
            return;
        }
        TFTP_INFO("Packet deserialized successfully, OpCode: %d", static_cast<int>(packet.GetOpCode()));
#ifdef _WIN32
    SOCKET client_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_sock == INVALID_SOCKET) {
#else
    int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_sock < 0) {
#endif
        TFTP_ERROR("Client socket creation failed");
        return;
    }
    TFTP_INFO("Client socket created successfully");
    
    sockaddr_in client = client_addr;
    client.sin_port = htons(0);
    if (bind(client_sock, (struct sockaddr*)&client, sizeof(client)) < 0) {
        TFTP_ERROR("Client socket bind failed");
        CLOSESOCKET(client_sock);
        return;
    }
    TFTP_INFO("Client socket bound successfully");
    TransferMode mode = packet.GetMode();
    std::string filename = packet.GetFilename();
    
    // Read configuration with shared lock to avoid race conditions
    bool is_secure_mode;
    {
        std::shared_lock<std::shared_mutex> lock(config_mutex_);
        is_secure_mode = secure_mode_;
    }
    
    TFTP_INFO("Processing packet - OpCode: %d, filename: %s, secure_mode: %s", 
             static_cast<int>(packet.GetOpCode()), filename.c_str(), is_secure_mode ? "true" : "false");
    if (is_secure_mode && !util::IsPathSecure(filename, root_dir_)) {
        TFTP_INFO("Path security check failed for: %s", filename.c_str());
        SendError(client_sock, client_addr, ErrorCode::kAccessViolation,
                 "Access denied");
        CLOSESOCKET(client_sock);
        return;
    }
    std::string filepath = util::NormalizePath(root_dir_ + filename);
    switch (packet.GetOpCode()) {
        case OpCode::kReadRequest:
            TFTP_INFO("Processing Read Request for file: %s", filename.c_str());
            HandleReadRequest(client_sock, client_addr, filepath, mode);
            break;
        case OpCode::kWriteRequest:
            TFTP_INFO("Processing Write Request for file: %s (options: %zu)", filename.c_str(), packet.GetOptions().size());
            HandleWriteRequest(client_sock, client_addr, filepath, mode, packet);
            break;
        default:
            TFTP_ERROR("Unknown operation code: %d", static_cast<int>(packet.GetOpCode()));
            SendError(client_sock, client_addr, ErrorCode::kIllegalOperation,
                     "Illegal operation");
            break;
    }
    CLOSESOCKET(client_sock);
    } catch (const std::exception& e) {
        TFTP_ERROR("Exception in HandleClient: %s", e.what());
    } catch (...) {
        TFTP_ERROR("Unknown exception in HandleClient");
    }
}

void TftpServerImpl::HandleReadRequest(
#ifdef _WIN32
    SOCKET sock,
#else
    int sock,
#endif
    const sockaddr_in& client_addr,
    const std::string& filepath, TransferMode mode) {
    TFTP_INFO("File read request: %s", filepath.c_str());

    (void)mode; // Suppress unused parameter warning

    // Read data with callback protection
    std::vector<uint8_t> file_data;
    bool success;
    size_t max_size;
    int timeout_secs;
    {
        std::shared_lock<std::shared_mutex> lock(config_mutex_);
        success = read_callback_(filepath, file_data);
        max_size = max_transfer_size_;
        timeout_secs = timeout_seconds_;
    }
    
    if (!success) {
        SendError(sock, client_addr, ErrorCode::kFileNotFound, "File not found");
        return;
    }
    
    if (file_data.size() > max_size) {
        SendError(sock, client_addr, ErrorCode::kDiskFull, "File size too large");
        return;
    }
    
    // Send data in blocks
    uint16_t block_number = 1;
    size_t offset = 0;
    bool last_packet = false;
    
    do {
        // Prepare next data block
        size_t remaining = file_data.size() - offset;
        size_t block_size = (remaining > kMaxDataSize) ? kMaxDataSize : remaining;
        
        // Create and send data packet with optimized vector creation
        std::vector<uint8_t> block_data;
        block_data.reserve(block_size);
        block_data.assign(file_data.begin() + offset, file_data.begin() + offset + block_size);
        
        TftpPacket data_packet = TftpPacket::CreateData(block_number, std::move(block_data));
        if (!SendPacket(sock, client_addr, data_packet)) {
            TFTP_ERROR("Data packet send failed");
            return;
        }
        
        // Wait for ACK
        TftpPacket ack_packet;
        sockaddr_in ack_addr = {};
        if (!ReceivePacket(sock, ack_addr, ack_packet, timeout_secs * 1000)) {
            TFTP_ERROR("ACK timeout");
            return;
        }
        
        // Validate ACK
        if (ack_packet.GetOpCode() != OpCode::kAcknowledge || ack_packet.GetBlockNumber() != block_number) {
            TFTP_ERROR("Invalid ACK");
            return;
        }
        
        // Prepare next block
        offset += block_size;
        block_number++;
        
        // Check if it's the last packet
        last_packet = (block_size < kMaxDataSize);
        
    } while (!last_packet);
    
    TFTP_INFO("File transfer completed: %s (%zu bytes)", filepath.c_str(), file_data.size());
}

void TftpServerImpl::HandleWriteRequest(
#ifdef _WIN32
    SOCKET sock,
#else
    int sock,
#endif
    const sockaddr_in& client_addr,
    const std::string& filepath, TransferMode mode, const TftpPacket& packet) {
    TFTP_INFO("File write request: %s", filepath.c_str());
    
    (void)mode; // Suppress unused parameter warning
    
    // Read configuration once to avoid multiple locks
    size_t max_size;
    int timeout_secs;
    {
        std::shared_lock<std::shared_mutex> lock(config_mutex_);
        max_size = max_transfer_size_;
        timeout_secs = timeout_seconds_;
    }
    
    // Get expected file size from tsize option
    size_t expected_file_size = 0;
    bool has_expected_size = false;
    if (packet.HasOption("tsize")) {
        std::string tsize_str = packet.GetOption("tsize");
        try {
            expected_file_size = std::stoull(tsize_str);
            has_expected_size = true;
            TFTP_INFO("Expected file size from tsize option: %zu bytes", expected_file_size);
        } catch (const std::exception& e) {
            TFTP_WARN("Invalid tsize option value: %s, error: %s", tsize_str.c_str(), e.what());
        }
    }
    
    // If options are present, send OACK, otherwise send normal ACK
    if (!packet.GetOptions().empty()) {
        TFTP_INFO("WRQ contains options, sending OACK");
        
        // Create OACK packet (manually serialize)
        std::vector<uint8_t> oack_data;
        
        // Add OpCode::kOACK (6) (network byte order)
        uint16_t oack_opcode_network = htons(6);
        oack_data.resize(2);
        std::memcpy(&oack_data[0], &oack_opcode_network, sizeof(uint16_t));
        
        // Add corresponding options
        for (const auto& option : packet.GetOptions()) {
            TFTP_INFO("Processing option: %s = %s", option.first.c_str(), option.second.c_str());
            
            // Add option name
            for (char c : option.first) {
                oack_data.push_back(static_cast<uint8_t>(c));
            }
            oack_data.push_back(0); // null-terminated
            
            // Add option value
            std::string value = option.second;
            
            // Process tsize option
            if (option.first == "tsize") {
                // If client sent actual file size, echo it back
                // If client sent 0, echo 0 back
                value = option.second;
                TFTP_INFO("Echoing back tsize value: %s", value.c_str());
            }
            // Process blksize option (support default 512 bytes)
            else if (option.first == "blksize") {
                try {
                    int blksize = std::stoi(option.second);
                    if (blksize >= 8 && blksize <= 65464) {
                        // Accept if requested block size is within valid range
                        value = option.second;
                    } else {
                        // Use default 512 if invalid
                        value = "512";
                    }
                    TFTP_INFO("Block size negotiated: %s", value.c_str());
                } catch (const std::exception& e) {
                    TFTP_WARN("Invalid blksize option: %s, using default 512", option.second.c_str());
                    value = "512";
                }
            }
            // Process timeout option (accept 1-255 seconds range)
            else if (option.first == "timeout") {
                try {
                    int timeout_val = std::stoi(option.second);
                    if (timeout_val >= 1 && timeout_val <= 255) {
                        value = option.second;
                    } else {
                        value = "6"; // Default value
                    }
                    TFTP_INFO("Timeout negotiated: %s seconds", value.c_str());
                } catch (const std::exception& e) {
                    TFTP_WARN("Invalid timeout option: %s, using default 6", option.second.c_str());
                    value = "6";
                }
            }
            
            for (char c : value) {
                oack_data.push_back(static_cast<uint8_t>(c));
            }
            oack_data.push_back(0); // null-terminated
        }
        
        // Send OACK (manual sendto)
        TFTP_INFO("Sending OACK packet with %zu bytes", oack_data.size());
        
        // OACK hex dump
        std::string hex_dump;
        for (size_t i = 0; i < oack_data.size(); ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", oack_data[i]);
            hex_dump += buf;
        }
        TFTP_INFO("OACK hex dump: %s", hex_dump.c_str());
        
        // Send OACK directly using sendto
        int sent_bytes = sendto(sock, (const char*)oack_data.data(), static_cast<int>(oack_data.size()), 0,
                              (struct sockaddr*)&client_addr, sizeof(client_addr));
        if (sent_bytes != static_cast<int>(oack_data.size())) {
            TFTP_ERROR("OACK send failed: sent=%d, expected=%zu", sent_bytes, oack_data.size());
            return;
        }
        TFTP_INFO("OACK sent successfully (%d bytes)", sent_bytes);
    } else {
        // If no options, send normal ACK 0
        TFTP_INFO("WRQ without options, sending ACK 0");
        TftpPacket ack_packet = TftpPacket::CreateAck(0);
        if (!SendPacket(sock, client_addr, ack_packet)) {
            TFTP_ERROR("ACK 0 send failed");
            return;
        }
        TFTP_INFO("ACK 0 sent successfully");
    }
    
    // Receive data
    std::vector<uint8_t> file_data;
    uint16_t expected_block = 1;
    bool last_packet = false;
    
    // If OACK was sent, the first data packet starts from block 1
    // If normal ACK was sent, the first data packet also starts from block 1
    
    do {
        // Receive data packet (using client-specific socket)
        TftpPacket data_packet;
        sockaddr_in data_addr = {};
        TFTP_INFO("Waiting for data packet #%d on client socket", expected_block);
        
        TFTP_INFO("Starting ReceivePacket call with timeout %d ms", timeout_secs * 1000);
        if (!ReceivePacket(sock, data_addr, data_packet, timeout_secs * 1000)) {
            TFTP_ERROR("Data packet receive timeout for block #%d", expected_block);
            return;
        }
        TFTP_INFO("ReceivePacket completed successfully");
        
        // Validate data packet
        TFTP_INFO("Checking packet opcode: %d", static_cast<int>(data_packet.GetOpCode()));
        if (data_packet.GetOpCode() != OpCode::kData) {
            TFTP_ERROR("Invalid packet (not a data packet): OpCode=%d", static_cast<int>(data_packet.GetOpCode()));
            SendError(sock, client_addr, ErrorCode::kIllegalOperation, "Illegal operation");
            return;
        }
        TFTP_INFO("Packet opcode validation passed");
        
        TFTP_INFO("Checking block number: received=%d, expected=%d", data_packet.GetBlockNumber(), expected_block);
        if (data_packet.GetBlockNumber() != expected_block) {
            TFTP_ERROR("Invalid block number: %d (expected: %d)", data_packet.GetBlockNumber(), expected_block);
            return;
        }
        TFTP_INFO("Block number validation passed");
        
        // Add data
        const std::vector<uint8_t>& block_data = data_packet.GetData();
        size_t prev_size = file_data.size();
        file_data.insert(file_data.end(), block_data.begin(), block_data.end());
        TFTP_INFO("Received data block #%d, prev_size=%zu, block_size=%zu bytes, total=%zu bytes", 
                 expected_block, prev_size, block_data.size(), file_data.size());
        
        // Send ACK (from client-specific socket to source address)
        TFTP_INFO("Creating ACK packet for block #%d", expected_block);
        TftpPacket ack_packet = TftpPacket::CreateAck(expected_block);
        TFTP_INFO("ACK packet created, attempting to send to data source");
        
        if (!SendPacket(sock, data_addr, ack_packet)) {
            TFTP_ERROR("ACK send failed for block #%d", expected_block);
            return;
        }
        TFTP_INFO("Sent ACK for block #%d successfully", expected_block);
        
        // Prepare next block
        expected_block++;
        
        // Check if it's the last packet
        // RFC 1350: Transfer ends when data packet size < 512 bytes
        // When using tsize option, still need to wait for termination packet if file size is multiple of 512
        bool size_based_completion = (block_data.size() < kMaxDataSize);
        last_packet = size_based_completion;
        
        TFTP_INFO("Block #%d completion check: size_based=%s (%zu<%zu), is_last=%s", 
                 expected_block-1, 
                 size_based_completion ? "true" : "false", block_data.size(), kMaxDataSize,
                 last_packet ? "YES" : "NO");
        
        // File size limit check
        if (file_data.size() > max_size) {
            TFTP_ERROR("File size exceeded limit: %zu > %zu", file_data.size(), max_size);
            SendError(sock, client_addr, ErrorCode::kDiskFull, "File size too large");
            return;
        }
        
        // Additional safety check: if using tsize option and received data exceeds expected size significantly
        if (has_expected_size && file_data.size() > expected_file_size + kMaxDataSize) {
            TFTP_ERROR("Received data significantly exceeds tsize: %zu > %zu + %zu", 
                      file_data.size(), expected_file_size, kMaxDataSize);
            SendError(sock, client_addr, ErrorCode::kDiskFull, "File size exceeds tsize");
            return;
        }
        
    } while (!last_packet);
    
    TFTP_INFO("All data received: %zu bytes, %d blocks. Writing to file...", file_data.size(), expected_block - 1);
    
    // Write file with callback protection
    TFTP_INFO("Calling write_callback_ for path: %s", filepath.c_str());
    bool success;
    {
        std::shared_lock<std::shared_mutex> lock(config_mutex_);
        success = write_callback_(filepath, file_data);
    }
    if (!success) {
        TFTP_ERROR("File write failed: %s", filepath.c_str());
        SendError(sock, client_addr, ErrorCode::kAccessViolation, "File write failed");
        return;
    }
    
    TFTP_INFO("File receive completed: %s (%zu bytes)", filepath.c_str(), file_data.size());
}

bool TftpServerImpl::SendPacket(
#ifdef _WIN32
    SOCKET sock,
#else
    int sock,
#endif
    const sockaddr_in& addr, const TftpPacket& packet) {
    std::vector<uint8_t> data = packet.Serialize();
    
    // Send packet hex dump (for ACK packets)
    if (packet.GetOpCode() == OpCode::kAcknowledge) {
        std::string hex_dump;
        for (size_t i = 0; i < data.size(); ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", data[i]);
            hex_dump += buf;
        }
        TFTP_INFO("Sending ACK packet (block %d): %s", packet.GetBlockNumber(), hex_dump.c_str());
    }
    
    int retries = 0;
    while (retries < kMaxRetries) {
        int sent_bytes = sendto(sock, (const char*)data.data(), static_cast<int>(data.size()), 0,
                              (struct sockaddr*)&addr, sizeof(addr));
        if (sent_bytes == static_cast<int>(data.size())) {
            return true;
        }
        TFTP_WARN("Packet send failed, retrying... (%d/%d)", retries + 1, kMaxRetries);
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryTimeoutMs));
        retries++;
    }
    TFTP_ERROR("Packet send failed, max retries reached");
    return false;
}

bool TftpServerImpl::ReceivePacket(
#ifdef _WIN32
    SOCKET sock,
#else
    int sock,
#endif
    sockaddr_in& addr, TftpPacket& packet,
    int timeout_ms) {
    TFTP_INFO("ReceivePacket: Starting with timeout %d ms", timeout_ms);
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    TFTP_INFO("ReceivePacket: Calling select() with timeout %d.%06d", tv.tv_sec, tv.tv_usec);
    
#ifdef _WIN32
    // Windows select does not depend on socket number, only fdset is checked, so +1 is not needed
    int result = select(0, &readfds, NULL, NULL, &tv);
#else
    // UNIX systems require socket number + 1
    int result = select(sock + 1, &readfds, NULL, NULL, &tv);
#endif
    
    TFTP_INFO("ReceivePacket: select() returned %d", result);
    
    if (result <= 0) {
        if (result == 0) {
            TFTP_WARN("Packet receive timeout");
        } else {
            TFTP_ERROR("select() error");
        }
        return false;
    }
    
    TFTP_INFO("ReceivePacket: Socket is ready for reading");
    
    uint8_t buffer[kMaxPacketSize] = {0};
#ifdef _WIN32
    int addrlen = sizeof(addr);
#else
    socklen_t addrlen = sizeof(addr);
#endif
    
    TFTP_INFO("ReceivePacket: Calling recvfrom()");
    int recv_bytes = recvfrom(sock, (char*)buffer, sizeof(buffer), 0,
                             (struct sockaddr*)&addr, &addrlen);
    
    TFTP_INFO("ReceivePacket: recvfrom() returned %d bytes", recv_bytes);
    
    if (recv_bytes <= 0) {
        TFTP_ERROR("Packet receive error");
        return false;
    }
    
    TFTP_INFO("ReceivePacket: Attempting to deserialize %d bytes", recv_bytes);
    if (!packet.Deserialize(std::vector<uint8_t>(buffer, buffer + recv_bytes))) {
        TFTP_ERROR("Invalid packet format");
        return false;
    }
    
    TFTP_INFO("ReceivePacket: Packet deserialized successfully");
    return true;
}

void TftpServerImpl::SendError(
#ifdef _WIN32
    SOCKET sock,
#else
    int sock,
#endif
    const sockaddr_in& addr, ErrorCode code,
    const std::string& message) {
    TFTP_INFO("SendError called - code: %d, message: %s", static_cast<int>(code), message.c_str());
    TftpPacket error_packet = TftpPacket::CreateError(code, message);
    
    // Attempt to send error packet multiple times
    bool sent = false;
    for (int retry = 0; retry < kMaxRetries && !sent; retry++) {
        TFTP_INFO("Attempting to send error packet, retry: %d", retry);
        sent = SendPacket(sock, addr, error_packet);
        if (!sent) {
            TFTP_WARN("Error packet send failed, retry: %d", retry);
            // Wait a bit before retrying
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryTimeoutMs / 2));
        }
    }
    
    if (sent) {
        TFTP_ERROR("Error sent: %s (code: %d)", message.c_str(), static_cast<int>(code));
    } else {
        TFTP_ERROR("Failed to send error after %d retries: %s (code: %d)", 
                  kMaxRetries, message.c_str(), static_cast<int>(code));
    }
}

bool TftpServerImpl::DefaultReadHandler(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        TFTP_ERROR("Cannot open file: %s", path.c_str());
        return false;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Prepare buffer with reserve first to avoid potential reallocation
    data.clear();
    data.reserve(static_cast<size_t>(file_size));
    data.resize(static_cast<size_t>(file_size));
    
    // Read file
    if (!file.read(reinterpret_cast<char*>(data.data()), file_size)) {
        TFTP_ERROR("File read error: %s", path.c_str());
        data.clear();
        return false;
    }
    
    return true;
}

bool TftpServerImpl::DefaultWriteHandler(const std::string& path, const std::vector<uint8_t>& data) {
    TFTP_INFO("DefaultWriteHandler called: path=%s, data_size=%zu", path.c_str(), data.size());
    
    // Ensure directory exists 
    std::filesystem::path file_path(path);
    std::filesystem::path parent_path = file_path.parent_path();
    
    TFTP_INFO("Parent directory: %s", parent_path.string().c_str());
    
    if (!parent_path.empty() && !std::filesystem::exists(parent_path)) {
        TFTP_INFO("Creating parent directory: %s", parent_path.string().c_str());
        try {
            std::filesystem::create_directories(parent_path);
            TFTP_INFO("Parent directory created successfully");
        } catch (const std::exception& e) {
            TFTP_ERROR("Directory creation error: %s (%s)", parent_path.string().c_str(), e.what());
            return false;
        }
    } else {
        TFTP_INFO("Parent directory already exists or is empty");
    }
    
    // Open file
    TFTP_INFO("Opening file for writing: %s", path.c_str());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        TFTP_ERROR("Cannot open file: %s", path.c_str());
        return false;
    }
    TFTP_INFO("File opened successfully");
    
    // Write data as is
    TFTP_INFO("Writing %zu bytes to file", data.size());
    if (!file.write(reinterpret_cast<const char*>(data.data()), data.size())) {
        TFTP_ERROR("File write error: %s", path.c_str());
        return false;
    }
    TFTP_INFO("Data written successfully");
    
    // Flush and explicitly close
    file.flush();
    file.close();
    TFTP_INFO("File flushed and closed");
    
    // Verify file was created
    if (!std::filesystem::exists(path)) {
        TFTP_ERROR("File verification failed after write: %s", path.c_str());
        return false;
    }
    TFTP_INFO("File verification successful");
    
    // Log success and size
    TFTP_INFO("File written successfully: %s (%zu bytes)", path.c_str(), data.size());
    
    return true;
}

} // namespace internal
} // namespace tftpserver 