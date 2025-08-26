#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Windowsのヘッダファイルのインクルード順序を修正
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <tftp/tftp_server.h>
#include <tftp/tftp_packet.h>
#include <tftp/tftp_common.h>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm> // Required for std::min
#include <cstring>
#include <filesystem>
#include <iostream> // Added for standard output

using namespace tftpserver;

// Log macros (fixed to avoid newline issues)
#ifdef _WIN32
#define LOG_INFO(fmt, ...) do { \
    fprintf(stdout, "[CLIENT INFO] " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
    char buf[1024]; \
    snprintf(buf, sizeof(buf), "[CLIENT INFO] " fmt, ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    OutputDebugStringA("\n"); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    fprintf(stderr, "[CLIENT ERROR] " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
    char buf[1024]; \
    snprintf(buf, sizeof(buf), "[CLIENT ERROR] " fmt, ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    OutputDebugStringA("\n"); \
} while(0)
#else
#define LOG_INFO(fmt, ...) fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__); fflush(stdout)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); fflush(stderr)
#endif

// Helper function to close socket cross-platform
inline void CloseSocket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

// Test constants
constexpr uint16_t kTestPort = 6969;
constexpr const char* kTestRootDir = "./test_files";

constexpr const char* kTestFile = "upload_test.txt";

// Expected line endings per OS
#ifdef _WIN32
// Windows standard is \r\n
constexpr const char* kTestContent = "This is a test file for TFTP upload.\n"
                                  "This file will be uploaded to the TFTP server.\n"
                                  "Testing multiple lines and content.\n";
#else
// Unix standard is \n
constexpr const char* kTestContent = "This is a test file for TFTP upload.\n"
                                  "This file will be uploaded to the TFTP server.\n"
                                  "Testing multiple lines and content.\n";
#endif

constexpr const char* kLargeTestFile = "large_test_file.dat";
constexpr size_t kLargeFileSize = 1024 * 1024 + 100; // 1MB + 100バイト（最後のブロックが512バイト未満になるように）

// Test class
class TftpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        std::filesystem::create_directories(kTestRootDir);
        
        // Create test file
        std::ofstream test_file(std::string(kTestRootDir) + "/" + kTestFile, std::ios::binary);
        test_file.write(kTestContent, strlen(kTestContent));
        test_file.close();

        // Create large test file
        CreateLargeTestFile();

#ifdef _WIN32
        // For Windows, execute WSAStartup
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    void TearDown() override {
        // Delete test directory
        std::filesystem::remove_all(kTestRootDir);

#ifdef _WIN32
        WSACleanup();
#endif
    }

    // Create large test file
    void CreateLargeTestFile() {
        std::ofstream large_file(std::string(kTestRootDir) + "/" + kLargeTestFile, std::ios::binary);
        std::vector<uint8_t> buffer(kLargeFileSize);
        
        // Fill with random data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& byte : buffer) {
            byte = static_cast<uint8_t>(dis(gen));
        }
        
        large_file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        large_file.close();
    }

    // Function to send TFTP packet as client
    bool SendTftpPacket(int sock, const sockaddr_in& server_addr, const std::vector<uint8_t>& data) {
        // Explicitly convert data size to int and ensure no overflow
        if (data.size() > INT_MAX) {
            return false; // Return failure if data size is too large
        }
        
        const int data_size = static_cast<int>(data.size());
        
#ifdef _WIN32
        const int sent = sendto(sock, reinterpret_cast<const char*>(data.data()), 
                        data_size, 0,
#else
        const ssize_t sent = sendto(sock, reinterpret_cast<const char*>(data.data()), 
                        data_size, 0,
#endif
                         reinterpret_cast<const struct sockaddr*>(&server_addr), sizeof(server_addr));
        
        return sent == data_size;
    }

    // Function to receive TFTP packet as client
    bool ReceiveTftpPacket(int sock, std::vector<uint8_t>& data, sockaddr_in& server_addr, int timeout_ms = 5000) {
        // Set timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int result = select(sock + 1, &readfds, NULL, NULL, &tv);
        if (result <= 0) {
            return false;
        }
        
        uint8_t buffer[1024]={0};  // Buffer larger than normal TFTP packet size
#ifdef _WIN32
        int addrlen = sizeof(server_addr);
#else
        socklen_t addrlen = sizeof(server_addr);
#endif

        int recvlen = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                              (struct sockaddr*)&server_addr, &addrlen);
        
        if (recvlen > 0) {
            data.assign(buffer, buffer + recvlen);
            return true;
        }
        return false;
    }
    
    // Helper function for file download
    bool DownloadFile(const std::string& remote_file, std::vector<uint8_t>& file_data) {
        // Create client socket
        int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (client_sock < 0) {
            LOG_ERROR("Failed to create client socket");
            return false;
        }
        
        // Set server address
        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
        server_addr.sin_port = htons(kTestPort);
        
        LOG_INFO("Starting file download: %s", remote_file.c_str());
        
        // Create and send RRQ packet - use just the filename, not the path
        std::string filename_only = std::filesystem::path(remote_file).filename().string();
        LOG_INFO("Using filename for RRQ: %s", filename_only.c_str());
        
        TftpPacket rrq_packet = TftpPacket::CreateReadRequest(filename_only, TransferMode::kOctet);
        if (!SendTftpPacket(client_sock, server_addr, rrq_packet.Serialize())) {
            LOG_ERROR("Failed to send RRQ packet");
            CloseSocket(client_sock);
            return false;
        }
        
        // Receive and process data packets
        std::vector<uint8_t> response;
        uint16_t expected_block = 1;
        bool last_packet = false;
        file_data.clear();
        
        do {
            if (!ReceiveTftpPacket(client_sock, response, server_addr)) {
                LOG_ERROR("Failed to receive data packet: block=%d", expected_block);
                CloseSocket(client_sock);
                return false;
            }
            
            TftpPacket response_packet;
            if (!response_packet.Deserialize(response)) {
                LOG_ERROR("Failed to parse packet");
                CloseSocket(client_sock);
                return false;
            }
            
            if (response_packet.GetOpCode() == OpCode::kError) {
                LOG_ERROR("Received error packet: code=%d, message=%s", 
                       static_cast<int>(response_packet.GetErrorCode()), 
                       response_packet.GetErrorMessage().c_str());
                CloseSocket(client_sock);
                return false;
            }
            
            if (response_packet.GetOpCode() != OpCode::kData || 
                response_packet.GetBlockNumber() != expected_block) {
                LOG_ERROR("Invalid data packet: OpCode=%d, ExpectedBlock=%d, ReceivedBlock=%d", 
                       static_cast<int>(response_packet.GetOpCode()), 
                       expected_block, response_packet.GetBlockNumber());
                CloseSocket(client_sock);
                return false;
            }
            
            // Add data
            const std::vector<uint8_t>& block_data = response_packet.GetData();
            file_data.insert(file_data.end(), block_data.begin(), block_data.end());
            
            // Send ACK
            TftpPacket ack_packet = TftpPacket::CreateAck(expected_block);
            if (!SendTftpPacket(client_sock, server_addr, ack_packet.Serialize())) {
                LOG_ERROR("Failed to send ACK packet: block=%d", expected_block);
                CloseSocket(client_sock);
                return false;
            }
            
            // Prepare for next block
            expected_block++;
            
            // Check if this was the last packet
            last_packet = (block_data.size() < 512);
            
            // Report progress for large files
            if (expected_block % 100 == 0) {
                LOG_INFO("Download progress: %zu bytes received (block=%d)", file_data.size(), expected_block - 1);
            }
            
        } while (!last_packet);
        
        LOG_INFO("File download completed: total %zu bytes, %d blocks", file_data.size(), expected_block - 1);
        
        CloseSocket(client_sock);
        return true;
    }
    
    // Helper function for file upload
    bool UploadFile(const std::string& remote_file, const std::vector<uint8_t>& file_data) {
        // Create client socket
        int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (client_sock < 0) {
            LOG_ERROR("Failed to create client socket");
            return false;
        }
        
        // Set server address
        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
        server_addr.sin_port = htons(kTestPort);
        
        LOG_INFO("Starting file upload: %s (total %zu bytes)", remote_file.c_str(), file_data.size());
        
        // Create and send WRQ packet - use just the filename, not the path
        std::string filename_only = std::filesystem::path(remote_file).filename().string();
        LOG_INFO("Using filename for WRQ: %s", filename_only.c_str());
        
        TftpPacket wrq_packet = TftpPacket::CreateWriteRequest(filename_only, TransferMode::kOctet);
        
        // Add tsize option to inform server of expected file size
        wrq_packet.SetOption("tsize", std::to_string(file_data.size()));
        LOG_INFO("Adding tsize option: %zu bytes", file_data.size());
        
        if (!SendTftpPacket(client_sock, server_addr, wrq_packet.Serialize())) {
            LOG_ERROR("Failed to send WRQ packet");
            CloseSocket(client_sock);
            return false;
        }
        
        // Receive initial response (could be ACK or OACK)
        std::vector<uint8_t> response;
        if (!ReceiveTftpPacket(client_sock, response, server_addr)) {
            LOG_ERROR("Failed to receive initial response");
            CloseSocket(client_sock);
            return false;
        }
        
        TftpPacket response_packet;
        if (!response_packet.Deserialize(response)) {
            LOG_ERROR("Failed to parse initial response");
            CloseSocket(client_sock);
            return false;
        }
        
        // Handle OACK or ACK response
        if (response_packet.GetOpCode() == OpCode::kOACK) {
            LOG_INFO("Received OACK with options");
            // OACK received - options negotiated, no block number check needed
        } else if (response_packet.GetOpCode() == OpCode::kAcknowledge && 
                   response_packet.GetBlockNumber() == 0) {
            LOG_INFO("Received initial ACK (block 0)");
            // Normal ACK received
        } else {
            LOG_ERROR("Invalid initial response: OpCode=%d, BlockNumber=%d", 
                   static_cast<int>(response_packet.GetOpCode()), response_packet.GetBlockNumber());
            CloseSocket(client_sock);
            return false;
        }
        
        // Split data into 512-byte blocks and send
        uint16_t block_number = 1;
        size_t offset = 0;
        size_t total_sent = 0;
        
        while (offset < file_data.size()) {
            // Prepare next block
            size_t remaining = file_data.size() - offset;
            size_t block_size = (remaining > 512) ? 512 : remaining;
            std::vector<uint8_t> block_data(file_data.begin() + offset, file_data.begin() + offset + block_size);
            
            // Send data packet
            TftpPacket data_packet = TftpPacket::CreateData(block_number, block_data);
            if (!SendTftpPacket(client_sock, server_addr, data_packet.Serialize())) {
                LOG_ERROR("Failed to send data packet: block=%d, size=%zu, offset=%zu", 
                       block_number, block_size, offset);
                CloseSocket(client_sock);
                return false;
            }
            
            // Receive ACK
            if (!ReceiveTftpPacket(client_sock, response, server_addr)) {
                LOG_ERROR("Failed to receive ACK: block=%d", block_number);
                CloseSocket(client_sock);
                return false;
            }
            
            // Important: Create a new TftpPacket object for each response
            TftpPacket ack_packet;
            if (!ack_packet.Deserialize(response) || 
                ack_packet.GetOpCode() != OpCode::kAcknowledge ||
                ack_packet.GetBlockNumber() != block_number) {
                LOG_ERROR("Invalid ACK: block=%d, ReceivedOpCode=%d, ReceivedBlockNumber=%d", 
                       block_number, static_cast<int>(ack_packet.GetOpCode()), static_cast<int>(ack_packet.GetBlockNumber()));
                CloseSocket(client_sock);
                return false;
            }
            
            // Move to next block
            offset += block_size;
            total_sent += block_size;
            
            // Report progress (only for large files)
            if (file_data.size() > 100*1024 && block_number % 100 == 0) {
                LOG_INFO("Upload progress: %zu/%zu bytes (%.1f%%, block=%d)", 
                       total_sent, file_data.size(), 
                       (100.0 * total_sent / file_data.size()), block_number);
            }
            
            block_number++;
        }
        
        LOG_INFO("File upload completed: total %zu bytes, %d blocks", 
               total_sent, block_number - 1);
        
        CloseSocket(client_sock);
        return true;
    }
};

// File upload test
TEST_F(TftpServerTest, FileUpload) {
    // Start server
    TftpServer server(kTestRootDir, kTestPort);
    ASSERT_TRUE(server.Start());
    
    // Wait for server to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create client socket
    int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(client_sock, 0);

    // Set server address
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(kTestPort);

    // Create and send WRQ packet
    TftpPacket wrq_packet = TftpPacket::CreateWriteRequest("upload_test.txt", TransferMode::kOctet);
    ASSERT_TRUE(SendTftpPacket(client_sock, server_addr, wrq_packet.Serialize()));

    // Wait for initial ACK
    std::vector<uint8_t> response;
    ASSERT_TRUE(ReceiveTftpPacket(client_sock, response, server_addr));

    TftpPacket response_packet;
    ASSERT_TRUE(response_packet.Deserialize(response));
    ASSERT_EQ(response_packet.GetOpCode(), OpCode::kAcknowledge);
    ASSERT_EQ(response_packet.GetBlockNumber(), 0);

    // Send data packet
    std::vector<uint8_t> file_data(kTestContent, kTestContent + strlen(kTestContent));
    TftpPacket data_packet = TftpPacket::CreateData(1, file_data);
    ASSERT_TRUE(SendTftpPacket(client_sock, server_addr, data_packet.Serialize()));

    // Wait for ACK
    ASSERT_TRUE(ReceiveTftpPacket(client_sock, response, server_addr));
    ASSERT_TRUE(response_packet.Deserialize(response));
    ASSERT_EQ(response_packet.GetOpCode(), OpCode::kAcknowledge);
    ASSERT_EQ(response_packet.GetBlockNumber(), 1);

    // Stop server
    server.Stop();
    CloseSocket(client_sock);

    // Wait a bit to ensure file writing is completed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify uploaded file
    std::string uploaded_file = std::string(kTestRootDir) + "/upload_test.txt";
    ASSERT_TRUE(std::filesystem::exists(uploaded_file));
    
    std::ifstream file(uploaded_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    ASSERT_EQ(content, kTestContent);
}

// File download test
TEST_F(TftpServerTest, FileDownload) {
    // Start server
    TftpServer server(kTestRootDir, kTestPort);
    ASSERT_TRUE(server.Start());
    
    // Wait for server to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create client socket
    int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(client_sock, 0);

    // Set server address
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(kTestPort);

    // Create and send RRQ packet
    TftpPacket rrq_packet = TftpPacket::CreateReadRequest(kTestFile, TransferMode::kOctet);
    ASSERT_TRUE(SendTftpPacket(client_sock, server_addr, rrq_packet.Serialize()));

    // Wait for data packet
    std::vector<uint8_t> response;
    ASSERT_TRUE(ReceiveTftpPacket(client_sock, response, server_addr));

    TftpPacket response_packet;
    ASSERT_TRUE(response_packet.Deserialize(response));
    ASSERT_EQ(response_packet.GetOpCode(), OpCode::kData);
    ASSERT_EQ(response_packet.GetBlockNumber(), 1);

    // Verify data
    std::vector<uint8_t> file_data = response_packet.GetData();
    
    // Send ACK
    TftpPacket ack_packet = TftpPacket::CreateAck(1);
    ASSERT_TRUE(SendTftpPacket(client_sock, server_addr, ack_packet.Serialize()));

    // Receive additional data packets if any
    bool last_packet = (file_data.size() < 512);
    uint16_t block_number = 2;
    
    while (!last_packet) {
        ASSERT_TRUE(ReceiveTftpPacket(client_sock, response, server_addr));
        ASSERT_TRUE(response_packet.Deserialize(response));
        ASSERT_EQ(response_packet.GetOpCode(), OpCode::kData);
        ASSERT_EQ(response_packet.GetBlockNumber(), block_number);
        
        // Add data
        const std::vector<uint8_t>& block_data = response_packet.GetData();
        file_data.insert(file_data.end(), block_data.begin(), block_data.end());
        
        // Send ACK
        ack_packet = TftpPacket::CreateAck(block_number);
        ASSERT_TRUE(SendTftpPacket(client_sock, server_addr, ack_packet.Serialize()));
        
        // Check if this was the last packet
        last_packet = (block_data.size() < 512);
        block_number++;
    }

    // Stop server
    server.Stop();
    CloseSocket(client_sock);

    // Verify downloaded data
    std::string downloaded_content(file_data.begin(), file_data.end());
    
    ASSERT_EQ(downloaded_content, kTestContent);
}

// Large file upload and download test
TEST_F(TftpServerTest, LargeFileTransfer) {
    // Get absolute path for test directory
    std::filesystem::path cwd = std::filesystem::current_path();
    std::string abs_test_dir = (cwd / kTestRootDir).string();
    
    LOG_INFO("Current working directory: %s", cwd.string().c_str());
    LOG_INFO("Absolute test directory: %s", abs_test_dir.c_str());
    
    // Make sure the directory exists
    std::filesystem::create_directories(abs_test_dir);
    
    // Start server with absolute path
    TftpServer server(abs_test_dir, kTestPort);
    server.SetTimeout(10); // Set timeout to 10 seconds
    ASSERT_TRUE(server.Start());
    
    // Wait for server to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test direct file writing to verify filesystem access
    std::string test_write_path = abs_test_dir + "/test_write.dat";
    LOG_INFO("Testing direct file write to: %s", test_write_path.c_str());
    
    {
        std::ofstream test_file(test_write_path, std::ios::binary);
        if (!test_file) {
            LOG_ERROR("Failed to open test file for writing: %s", test_write_path.c_str());
        } else {
            // Write some test data
            std::vector<uint8_t> test_data(1024, 0x42);  // 1KB of 'B' bytes
            test_file.write(reinterpret_cast<const char*>(test_data.data()), test_data.size());
            test_file.flush();
            test_file.close();
            
            if (std::filesystem::exists(test_write_path)) {
                LOG_INFO("Direct file write test succeeded, size: %llu bytes", 
                        std::filesystem::file_size(test_write_path));
            } else {
                LOG_ERROR("Direct file write test failed - file does not exist after write");
            }
        }
    }
    
    // Prepare original file
    std::string large_test_file_path = abs_test_dir + "/" + kLargeTestFile;
    LOG_INFO("Large test file path: %s", large_test_file_path.c_str());
    
    // Create file if it doesn't exist
    if (!std::filesystem::exists(large_test_file_path)) {
        LOG_INFO("Test file does not exist, creating it");
        
        // Create large test file with explicit path
        std::ofstream large_file(large_test_file_path, std::ios::binary);
        ASSERT_TRUE(large_file.good()) << "Failed to create large test file";
        
        std::vector<uint8_t> buffer(kLargeFileSize);
        
        // Fill with random data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& byte : buffer) {
            byte = static_cast<uint8_t>(dis(gen));
        }
        
        large_file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        large_file.flush();
        large_file.close();
        
        LOG_INFO("Created large test file: %s", large_test_file_path.c_str());
    }
    
    // Verify file exists
    ASSERT_TRUE(std::filesystem::exists(large_test_file_path));
    
    // Read original file
    std::ifstream input_file(large_test_file_path, std::ios::binary);
    ASSERT_TRUE(input_file.good());
    
    std::vector<uint8_t> original_data(
        (std::istreambuf_iterator<char>(input_file)),
        std::istreambuf_iterator<char>()
    );
    input_file.close();
    
    LOG_INFO("Test started: original file size = %zu bytes", original_data.size());
    ASSERT_EQ(original_data.size(), kLargeFileSize);
    
    // Upload filename
    std::string upload_filename = "uploaded_large_file.dat";
    std::string uploaded_filepath = abs_test_dir + "/" + upload_filename;
    
    // Delete the file if it already exists
    if (std::filesystem::exists(uploaded_filepath)) {
        LOG_INFO("Removing existing file: %s", uploaded_filepath.c_str());
        std::filesystem::remove(uploaded_filepath);
    }
    
    // Test writing directly to the upload file
    {
        LOG_INFO("Testing direct write to upload file before TFTP upload: %s", uploaded_filepath.c_str());
        std::ofstream test_file(uploaded_filepath, std::ios::binary);
        if (!test_file) {
            LOG_ERROR("Failed to open upload file for direct writing: %s", uploaded_filepath.c_str());
        } else {
            test_file.write("TEST", 4);
            test_file.flush();
            test_file.close();
            
            if (std::filesystem::exists(uploaded_filepath)) {
                LOG_INFO("Direct write to upload file succeeded: size=%llu bytes", 
                       std::filesystem::file_size(uploaded_filepath));
                
                // Remove the file so it doesn't interfere with the test
                std::filesystem::remove(uploaded_filepath);
                LOG_INFO("Removed test file for upload test");
            } else {
                LOG_ERROR("Direct write to upload file failed - file not found after write");
            }
        }
    }
    
    // Upload file
    LOG_INFO("Starting upload: %s", upload_filename.c_str());
    ASSERT_TRUE(UploadFile(upload_filename, original_data));
    
    // Wait for upload to complete (handle async processing or filesystem delays)
    bool file_exists = false;
    LOG_INFO("Waiting for uploaded file to appear: %s", uploaded_filepath.c_str());
    for (int retry = 0; retry < 30; ++retry) {
        if (std::filesystem::exists(uploaded_filepath)) {
            file_exists = true;
            LOG_INFO("Uploaded file check: found on attempt %d", retry + 1);
            
            // Check file size
            size_t uploaded_size = std::filesystem::file_size(uploaded_filepath);
            LOG_INFO("Found uploaded file with size: %zu bytes", uploaded_size);
            
            // Verify file size matches original data size
            if (uploaded_size == original_data.size()) {
                LOG_INFO("File size matches expected size");
                break;
            } else {
                LOG_INFO("File size mismatch: expected %zu, got %zu - waiting for complete file", 
                        original_data.size(), uploaded_size);
                file_exists = false;  // Reset flag to keep waiting
            }
        }
        
        if (retry % 20 == 0) {
            LOG_INFO("Still waiting for file (attempt %d)...", retry);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // List all files in the directory
    LOG_INFO("Listing files in directory: %s", abs_test_dir.c_str());
    for (const auto& entry : std::filesystem::directory_iterator(abs_test_dir)) {
        LOG_INFO("Found file: %s, size: %lld bytes", 
                entry.path().filename().string().c_str(), 
                entry.file_size());
    }
    
    ASSERT_TRUE(file_exists) << "Uploaded file not found at: " << uploaded_filepath;
    
    if (file_exists) {
        size_t uploaded_size = std::filesystem::file_size(uploaded_filepath);
        LOG_INFO("Uploaded file size: %zu bytes", uploaded_size);
        ASSERT_EQ(uploaded_size, original_data.size());
        
        // Download file
        LOG_INFO("Starting download: %s", upload_filename.c_str());
        std::vector<uint8_t> downloaded_data;
        ASSERT_TRUE(DownloadFile(upload_filename, downloaded_data));
        
        LOG_INFO("Downloaded data size: %zu bytes", downloaded_data.size());
        ASSERT_EQ(downloaded_data.size(), original_data.size());
        
        // Compare the data
        bool data_matches = true;
        size_t mismatch_pos = 0;
        
        for (size_t i = 0; i < downloaded_data.size() && i < original_data.size(); ++i) {
            if (downloaded_data[i] != original_data[i]) {
                data_matches = false;
                mismatch_pos = i;
                break;
            }
        }
        
        if (!data_matches) {
            LOG_INFO("First mismatch at position: %zu", mismatch_pos);
            LOG_INFO("Original byte: 0x%02X, Downloaded byte: 0x%02X", 
                    original_data[mismatch_pos], downloaded_data[mismatch_pos]);
        }
        
        ASSERT_TRUE(data_matches);
    }

    // Stop server
    server.Stop();
}

// Async upload and download test
TEST_F(TftpServerTest, AsyncFileTransfer) {
    // Create test files in the same directory as other working tests (kTestRootDir)
    // This ensures consistency with SetUp() and other tests
    std::vector<std::string> test_files;
    for (int i = 0; i < 5; i++) {
        std::string filename = "async_test_" + std::to_string(i) + ".txt";
        std::string content = "This is async test file " + std::to_string(i) + "\n";
        content += "With multiple lines\n";
        content += "For testing purpose\n";
        
        // Use kTestRootDir like other working tests
        std::string full_path = std::string(kTestRootDir) + "/" + filename;
        std::ofstream file(full_path);
        if (!file.is_open()) {
            LOG_ERROR("Failed to create file: %s", full_path.c_str());
            ASSERT_TRUE(false) << "Failed to create test file: " << full_path;
        }
        file << content;
        file.flush();
        file.close();
        
        // Verify file was created
        if (!std::filesystem::exists(full_path)) {
            LOG_ERROR("File was not created: %s", full_path.c_str());
            ASSERT_TRUE(false) << "Test file was not created: " << full_path;
        }
        
        test_files.push_back(filename);
    }
    
    // Verify all files were created successfully
    for (const auto& filename : test_files) {
        std::string full_path = std::string(kTestRootDir) + "/" + filename;
        if (!std::filesystem::exists(full_path)) {
            ASSERT_TRUE(false) << "Test file was not created: " << full_path;
        }
    }
    
    // Give time for filesystem to ensure files are committed to disk
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Start server with same directory where files were created (like other tests do)
    TftpServer server(kTestRootDir, kTestPort);
    ASSERT_TRUE(server.Start());
    
    // Wait for server to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Execute parallel downloads
    std::vector<std::thread> download_threads;
    std::vector<std::vector<uint8_t>> download_results(test_files.size());
    std::vector<bool> download_success(test_files.size(), false);
    
    for (size_t i = 0; i < test_files.size(); i++) {
        download_threads.emplace_back([&, i]() {
            download_success[i] = DownloadFile(test_files[i], download_results[i]);
        });
    }
    
    // Wait for threads to complete
    for (auto& thread : download_threads) {
        thread.join();
    }
    
    // Verify results
    for (size_t i = 0; i < test_files.size(); i++) {
        ASSERT_TRUE(download_success[i]) << "Failed to download file " << test_files[i];
        
        // Compare with original file
        std::ifstream file(std::string(kTestRootDir) + "/" + test_files[i]);
        std::string expected_content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
        file.close();
        
        std::string actual_content(download_results[i].begin(), download_results[i].end());
        
#ifdef _WIN32
        // Allow for line ending conversion on Windows
        std::string expected_with_crlf = expected_content;
        size_t pos = 0;
        // Replace single \n with \r\n
        while ((pos = expected_with_crlf.find("\n", pos)) != std::string::npos) {
            if (pos == 0 || expected_with_crlf[pos - 1] != '\r') {
                expected_with_crlf.replace(pos, 1, "\r\n");
                pos += 2;
            } else {
                pos += 1;
            }
        }
        ASSERT_EQ(actual_content, expected_with_crlf) << "Content mismatch for file " << test_files[i];
#else
        ASSERT_EQ(actual_content, expected_content) << "Content mismatch for file " << test_files[i];
#endif
    }
    
    // Stop server
    server.Stop();
}

// Server start/stop test
TEST_F(TftpServerTest, StartStop) {
    TftpServer server(kTestRootDir, kTestPort);
    EXPECT_FALSE(server.IsRunning());
    
    ASSERT_TRUE(server.Start());
    EXPECT_TRUE(server.IsRunning());
    
    server.Stop();
    EXPECT_FALSE(server.IsRunning());
}

// Error handling test
TEST_F(TftpServerTest, ErrorHandling) {
    TftpServer server(kTestRootDir, kTestPort);
    ASSERT_TRUE(server.Start());
    
    // Wait for server to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(kTestPort);

    // Test RRQ to non-existent file (simpler test)
    int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(client_sock, 0);

    TftpPacket rrq_packet = TftpPacket::CreateReadRequest("nonexistent_file.txt", TransferMode::kOctet);
    ASSERT_TRUE(SendTftpPacket(client_sock, server_addr, rrq_packet.Serialize()));

    // Wait for error packet
    std::vector<uint8_t> response;
    ASSERT_TRUE(ReceiveTftpPacket(client_sock, response, server_addr, 2000));
    
    TftpPacket response_packet;
    ASSERT_TRUE(response_packet.Deserialize(response));
    ASSERT_EQ(response_packet.GetOpCode(), OpCode::kError);
    ASSERT_EQ(static_cast<int>(response_packet.GetErrorCode()), static_cast<int>(ErrorCode::kFileNotFound));

    server.Stop();
    CloseSocket(client_sock);
}

int main(int argc, char** argv) {
    std::cout << "=== Custom main function called ===" << std::endl;
    std::cout << "Number of arguments: " << argc << std::endl;
    for (int i = 0; i < argc; ++i) {
        std::cout << "Arg[" << i << "]: " << argv[i] << std::endl;
    }
    
    // GoogleTest情報を表示
    std::cout << "=== Before InitGoogleTest ===" << std::endl;
    
    ::testing::InitGoogleTest(&argc, argv);
    std::cout << "=== After InitGoogleTest ===" << std::endl;
    
    // GoogleTestが検出したテスト数を確認（簡略化）
    std::cout << "Starting test execution..." << std::endl;
    
    int result = RUN_ALL_TESTS();
    std::cout << "=== Test result: " << result << " ===" << std::endl;
    return result;
}

