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
// kMaxPacketSizeとkMaxDataSizeはtftp_common.hで既に定義されているため、ここでは再定義しない

TftpServerImpl::TftpServerImpl(const std::string& root_dir, uint16_t port)
    : root_dir_(root_dir),
      port_(port),
#ifdef _WIN32
      sockfd_(INVALID_SOCKET),
#else
      sockfd_(-1),
#endif
      running_(false),
      secure_mode_(true),
      max_transfer_size_(1024 * 1024 * 1024),  // 1MB
      timeout_seconds_(5) {
    if (!root_dir_.empty() && root_dir_.back() != '/' && root_dir_.back() != '\\') {
        root_dir_ += '/';
    }
    
    // デフォルトのコールバック設定
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
    running_ = true;
    server_thread_ = std::thread(&TftpServerImpl::ServerLoop, this);
    TFTP_INFO("TFTP server started on port %d", port_);
    return true;
}

void TftpServerImpl::Stop() {
    if (!running_) return;
    
    // フラグを先に設定して、ServerLoopが早期に終了するようにする
    running_ = false;
    
    // ソケットをクローズ
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
    
    // スレッドの終了を待機
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    TFTP_INFO("TFTP server stopped");
}

bool TftpServerImpl::IsRunning() const {
    // サーバーが実行中かどうかを複数の条件でチェック
    if (!running_) {
        // runningフラグがfalseならサーバーは停止している
        return false;
    }
    
    // ソケットが有効かどうかもチェック
#ifdef _WIN32
    if (sockfd_ == INVALID_SOCKET) {
#else
    if (sockfd_ < 0) {
#endif
        // ソケットが無効ならサーバーは実行していない
        return false;
    }
    
    // スレッドがjoin可能かどうかもチェック
    if (!server_thread_.joinable()) {
        // スレッドがjoin不可能ならサーバーは実行していない
        return false;
    }
    
    // すべての条件を満たせばサーバーは実行中
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
            std::thread client_thread(&TftpServerImpl::HandleClient, this,
                                    std::vector<uint8_t>(buffer, buffer + recvlen),
                                    client_addr);
            client_thread.detach();
        }
    }
}

void TftpServerImpl::HandleClient(const std::vector<uint8_t>& initial_packet,
                                   const sockaddr_in& client_addr) {
    try {
        TFTP_INFO("HandleClient called with packet size: %zu", initial_packet.size());
        
        // デバッグ: パケットの内容を16進数でダンプ
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
    TFTP_INFO("Processing packet - OpCode: %d, filename: %s, secure_mode: %s", 
             static_cast<int>(packet.GetOpCode()), filename.c_str(), secure_mode_ ? "true" : "false");
    if (secure_mode_ && !util::IsPathSecure(filename, root_dir_)) {
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

    // データの読み込み
    std::vector<uint8_t> file_data;
    bool success = read_callback_(filepath, file_data);
    
    if (!success) {
        SendError(sock, client_addr, ErrorCode::kFileNotFound, "File not found");
        return;
    }
    
    if (file_data.size() > max_transfer_size_) {
        SendError(sock, client_addr, ErrorCode::kDiskFull, "File size too large");
        return;
    }
    
    // ブロック単位でデータを送信
    uint16_t block_number = 1;
    size_t offset = 0;
    bool last_packet = false;
    
    do {
        // 次のデータブロックの準備
        size_t remaining = file_data.size() - offset;
        size_t block_size = (remaining > kMaxDataSize) ? kMaxDataSize : remaining;
        std::vector<uint8_t> block_data(file_data.begin() + offset, file_data.begin() + offset + block_size);
        
        // データパケットの作成と送信
        TftpPacket data_packet = TftpPacket::CreateData(block_number, block_data);
        if (!SendPacket(sock, client_addr, data_packet)) {
            TFTP_ERROR("Data packet send failed");
            return;
        }
        
        // ACK待機
        TftpPacket ack_packet;
        sockaddr_in ack_addr = {};
        if (!ReceivePacket(sock, ack_addr, ack_packet, timeout_seconds_ * 1000)) {
            TFTP_ERROR("ACK timeout");
            return;
        }
        
        // ACKの検証
        if (ack_packet.GetOpCode() != OpCode::kAcknowledge || ack_packet.GetBlockNumber() != block_number) {
            TFTP_ERROR("Invalid ACK");
            return;
        }
        
        // 次のブロックの準備
        offset += block_size;
        block_number++;
        
        // 最後のパケットかどうかをチェック
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
    
    // tsizeオプションから期待ファイルサイズを取得
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
    
    // オプションがある場合はOACK、ない場合は通常のACKを送信
    if (!packet.GetOptions().empty()) {
        TFTP_INFO("WRQ contains options, sending OACK");
        
        // OACKパケットの作成（手動でシリアライズ）
        std::vector<uint8_t> oack_data;
        
        // OpCode::kOACK (6) を追加（ネットワークバイトオーダー）
        uint16_t oack_opcode_network = htons(6);
        oack_data.resize(2);
        std::memcpy(&oack_data[0], &oack_opcode_network, sizeof(uint16_t));
        
        // 対応するオプションを追加
        for (const auto& option : packet.GetOptions()) {
            TFTP_INFO("Processing option: %s = %s", option.first.c_str(), option.second.c_str());
            
            // オプション名を追加
            for (char c : option.first) {
                oack_data.push_back(static_cast<uint8_t>(c));
            }
            oack_data.push_back(0); // null終端
            
            // オプション値を追加
            std::string value = option.second;
            
            // tsizeオプションの処理
            if (option.first == "tsize") {
                // WRQでクライアントが実際のファイルサイズを送信した場合、それをエコーバックする
                // クライアントが0を送信した場合は0をエコーバックする
                value = option.second;
                TFTP_INFO("Echoing back tsize value: %s", value.c_str());
            }
            // blksizeオプションの処理（デフォルトの512バイトをサポート）
            else if (option.first == "blksize") {
                try {
                    int blksize = std::stoi(option.second);
                    if (blksize >= 8 && blksize <= 65464) {
                        // 要求されたブロックサイズが有効範囲内なら受け入れる
                        value = option.second;
                    } else {
                        // 無効な場合はデフォルトの512を使用
                        value = "512";
                    }
                    TFTP_INFO("Block size negotiated: %s", value.c_str());
                } catch (const std::exception& e) {
                    TFTP_WARN("Invalid blksize option: %s, using default 512", option.second.c_str());
                    value = "512";
                }
            }
            // timeoutオプションの処理（1-255秒の範囲で受け入れ）
            else if (option.first == "timeout") {
                try {
                    int timeout_val = std::stoi(option.second);
                    if (timeout_val >= 1 && timeout_val <= 255) {
                        value = option.second;
                    } else {
                        value = "6"; // デフォルト値
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
            oack_data.push_back(0); // null終端
        }
        
        // OACK送信（手動sendto方式）
        TFTP_INFO("Sending OACK packet with %zu bytes", oack_data.size());
        
        // OACKパケットのヘックスダンプ
        std::string hex_dump;
        for (size_t i = 0; i < oack_data.size(); ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", oack_data[i]);
            hex_dump += buf;
        }
        TFTP_INFO("OACK hex dump: %s", hex_dump.c_str());
        
        // 直接sendtoでOACKを送信
        int sent_bytes = sendto(sock, (const char*)oack_data.data(), static_cast<int>(oack_data.size()), 0,
                              (struct sockaddr*)&client_addr, sizeof(client_addr));
        if (sent_bytes != static_cast<int>(oack_data.size())) {
            TFTP_ERROR("OACK send failed: sent=%d, expected=%zu", sent_bytes, oack_data.size());
            return;
        }
        TFTP_INFO("OACK sent successfully (%d bytes)", sent_bytes);
    } else {
        // オプションがない場合は通常のACK 0を送信
        TFTP_INFO("WRQ without options, sending ACK 0");
        TftpPacket ack_packet = TftpPacket::CreateAck(0);
        if (!SendPacket(sock, client_addr, ack_packet)) {
            TFTP_ERROR("ACK 0 send failed");
            return;
        }
        TFTP_INFO("ACK 0 sent successfully");
    }
    
    // データの受信
    std::vector<uint8_t> file_data;
    uint16_t expected_block = 1;
    bool last_packet = false;
    
    // OACKを送信した場合、最初のデータパケットはブロック1から始まる
    // 通常のACKを送信した場合も、最初のデータパケットはブロック1から始まる
    
    do {
        // データパケットの受信（クライアント専用ソケットを使用）
        TftpPacket data_packet;
        sockaddr_in data_addr = {};
        TFTP_INFO("Waiting for data packet #%d on client socket", expected_block);
        
        TFTP_INFO("Starting ReceivePacket call with timeout %d ms", timeout_seconds_ * 1000);
        if (!ReceivePacket(sock, data_addr, data_packet, timeout_seconds_ * 1000)) {
            TFTP_ERROR("Data packet receive timeout for block #%d", expected_block);
            return;
        }
        TFTP_INFO("ReceivePacket completed successfully");
        
        // データパケットの検証
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
        
        // データを追加
        const std::vector<uint8_t>& block_data = data_packet.GetData();
        size_t prev_size = file_data.size();
        file_data.insert(file_data.end(), block_data.begin(), block_data.end());
        TFTP_INFO("Received data block #%d, prev_size=%zu, block_size=%zu bytes, total=%zu bytes", 
                 expected_block, prev_size, block_data.size(), file_data.size());
        
        // ACKを送信（クライアント専用ソケットから送信元アドレスに）
        TFTP_INFO("Creating ACK packet for block #%d", expected_block);
        TftpPacket ack_packet = TftpPacket::CreateAck(expected_block);
        TFTP_INFO("ACK packet created, attempting to send to data source");
        
        if (!SendPacket(sock, data_addr, ack_packet)) {
            TFTP_ERROR("ACK send failed for block #%d", expected_block);
            return;
        }
        TFTP_INFO("Sent ACK for block #%d successfully", expected_block);
        
        // 次のブロックの準備
        expected_block++;
        
        // 最後のパケットかどうかをチェック（データサイズが512バイト未満、またはファイルサイズが目標サイズに達した場合）
        bool size_based_completion = (block_data.size() < kMaxDataSize);
        bool expected_size_completion = has_expected_size && (file_data.size() >= expected_file_size);
        last_packet = size_based_completion || expected_size_completion;
        
        TFTP_INFO("Block #%d completion check: size_based=%s (%zu<%zu), expected_size=%s (%zu>=%zu), is_last=%s", 
                 expected_block-1, 
                 size_based_completion ? "true" : "false", block_data.size(), kMaxDataSize,
                 expected_size_completion ? "true" : "false", file_data.size(), expected_file_size,
                 last_packet ? "YES" : "NO");
        
        // ファイルサイズの上限チェック
        if (file_data.size() > max_transfer_size_) {
            TFTP_ERROR("File size exceeded limit: %zu > %zu", file_data.size(), max_transfer_size_);
            SendError(sock, client_addr, ErrorCode::kDiskFull, "File size too large");
            return;
        }
        
    } while (!last_packet);
    
    TFTP_INFO("All data received: %zu bytes, %d blocks. Writing to file...", file_data.size(), expected_block - 1);
    
    // ファイルの書き込み
    TFTP_INFO("Calling write_callback_ for path: %s", filepath.c_str());
    bool success = write_callback_(filepath, file_data);
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
    
    // 送信パケットの16進ダンプ（ACKパケットの場合）
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
    // Windowsのselectはソケットの数値に関係なく、fdsetだけを見るので+1は不要
    int result = select(0, &readfds, NULL, NULL, &tv);
#else
    // UNIXシステムではソケット番号+1が必要
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
    
    // エラーパケットの送信を複数回試行
    bool sent = false;
    for (int retry = 0; retry < kMaxRetries && !sent; retry++) {
        TFTP_INFO("Attempting to send error packet, retry: %d", retry);
        sent = SendPacket(sock, addr, error_packet);
        if (!sent) {
            TFTP_WARN("Error packet send failed, retry: %d", retry);
            // 少し待ってから再試行
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
    
    // ファイルサイズを取得
    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // バッファを準備
    data.resize(static_cast<size_t>(file_size));
    
    // ファイルを読み込む
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