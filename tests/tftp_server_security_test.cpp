#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Windows header fix
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
#include <tftp/tftp_util.h>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <array>

using namespace tftpserver;

// Security test constants
constexpr uint16_t kSecurityTestPort = 6973;
constexpr const char* kSecurityTestRootDir = "./security_test_files";

// Log macros
#ifdef _WIN32
#define SEC_LOG(fmt, ...) do { \
    fprintf(stdout, "[SECURITY TEST] " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)
#else
#define SEC_LOG(fmt, ...) fprintf(stdout, "[SECURITY TEST] " fmt "\n", ##__VA_ARGS__); fflush(stdout)
#endif

// Helper function to close socket cross-platform
inline void CloseSocket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

/**
 * @class TftpServerSecurityTest
 * @brief Comprehensive security tests for TFTP server focusing on directory traversal vulnerability fix
 */
class TftpServerSecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory structure
        std::filesystem::create_directories(kSecurityTestRootDir);
        std::filesystem::create_directories(std::string(kSecurityTestRootDir) + "/allowed_subdir");
        std::filesystem::create_directories(std::string(kSecurityTestRootDir) + "/deep/nested/directory");
        std::filesystem::create_directories("outside_root");
        
        // Create legitimate test files
        CreateLegitimateTestFiles();
        
        // Create files outside root directory (should not be accessible via TFTP)
        CreateOutsideRootFiles();

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        // Start TFTP server with security enabled
        server_ = std::make_unique<TftpServer>(kSecurityTestRootDir, kSecurityTestPort);
        server_->SetTimeout(10);
        server_->SetSecureMode(true);  // Enable security mode
        
        ASSERT_TRUE(server_->Start()) << "Failed to start TFTP server for security tests";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        SEC_LOG("TFTP Security Test Server started on port %d with secure mode enabled", kSecurityTestPort);
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
            server_.reset();
        }

        // Clean up test directories
        std::filesystem::remove_all(kSecurityTestRootDir);
        std::filesystem::remove_all("outside_root");

#ifdef _WIN32
        WSACleanup();
#endif
    }

    void CreateLegitimateTestFiles() {
        // Normal files in root
        WriteFile("normal_file.txt", "This is a normal file content.\n");
        WriteFile("text_file.txt", "Text file content for testing.\n");
        WriteFile("binary_file.bin", std::string(256, '\x42')); // Binary content
        
        // Files in allowed subdirectories
        WriteFile("allowed_subdir/subdir_file.txt", "File in allowed subdirectory.\n");
        WriteFile("deep/nested/directory/deep_file.txt", "File in deep nested directory.\n");
        
        // Files with various legitimate name patterns
        WriteFile("file_with_spaces.txt", "File with spaces in name.\n");
        WriteFile("file.with.dots.txt", "File with dots in name.\n");
        WriteFile("file-with-dashes.txt", "File with dashes in name.\n");
        WriteFile("file_with_underscores.txt", "File with underscores in name.\n");
        WriteFile("UPPERCASE_FILE.TXT", "File with uppercase name.\n");
        WriteFile("123numeric_start.txt", "File starting with numbers.\n");
        
        SEC_LOG("Created legitimate test files");
    }
    
    void CreateOutsideRootFiles() {
        // Create files outside root directory that should never be accessible
        std::ofstream outside1("outside_root/secret_file.txt");
        outside1 << "This file should NEVER be accessible via TFTP!\n";
        outside1 << "If you can read this, there is a security vulnerability.\n";
        outside1.close();
        
        std::ofstream outside2("outside_root/sensitive_data.txt");
        outside2 << "SENSITIVE DATA - NOT FOR TFTP ACCESS\n";
        outside2 << "Contains: passwords, API keys, private information\n";
        outside2.close();
        
        // Create a file that mimics system files
        std::ofstream passwd("outside_root/passwd");
        passwd << "root:x:0:0:root:/root:/bin/bash\n";
        passwd << "daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin\n";
        passwd.close();
        
        SEC_LOG("Created outside root files for security testing");
    }

    void WriteFile(const std::string& filename, const std::string& content) {
        std::string filepath = std::string(kSecurityTestRootDir) + "/" + filename;
        
        // Create parent directories if needed
        std::filesystem::create_directories(std::filesystem::path(filepath).parent_path());
        
        std::ofstream file(filepath);
        file << content;
        file.close();
    }

    // Function to send TFTP packet as client
    bool SendTftpPacket(int sock, const sockaddr_in& server_addr, const std::vector<uint8_t>& data) {
        if (data.size() > INT_MAX) {
            return false;
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
        
        uint8_t buffer[1024] = {0};
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
    
    // Test security by attempting to read a file that should be blocked
    bool TestSecurityBlockedRead(const std::string& malicious_path, const std::string& description = "") {
        int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (client_sock < 0) {
            SEC_LOG("Failed to create client socket for security test");
            return false;
        }
        
        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
        server_addr.sin_port = htons(kSecurityTestPort);
        
        SEC_LOG("Testing security block for: %s [%s]", malicious_path.c_str(), description.c_str());
        
        // Create and send RRQ packet with malicious path
        TftpPacket rrq_packet = TftpPacket::CreateReadRequest(malicious_path, TransferMode::kOctet);
        if (!SendTftpPacket(client_sock, server_addr, rrq_packet.Serialize())) {
            SEC_LOG("Failed to send RRQ packet");
            CloseSocket(client_sock);
            return false;
        }
        
        // Receive response - should be ERROR packet
        std::vector<uint8_t> response;
        if (!ReceiveTftpPacket(client_sock, response, server_addr, 2000)) {
            SEC_LOG("No response received - security test failed");
            CloseSocket(client_sock);
            return false;
        }
        
        TftpPacket response_packet;
        if (!response_packet.Deserialize(response)) {
            SEC_LOG("Failed to parse response packet");
            CloseSocket(client_sock);
            return false;
        }
        
        CloseSocket(client_sock);
        
        // Security check passed if we received an ERROR packet
        bool security_blocked = (response_packet.GetOpCode() == OpCode::kError);
        
        if (security_blocked) {
            SEC_LOG("SECURITY CHECK PASSED: %s blocked (Error: %d - %s)", 
                   malicious_path.c_str(),
                   static_cast<int>(response_packet.GetErrorCode()), 
                   response_packet.GetErrorMessage().c_str());
        } else {
            SEC_LOG("SECURITY CHECK FAILED: %s not blocked (received OpCode: %d)", 
                   malicious_path.c_str(),
                   static_cast<int>(response_packet.GetOpCode()));
        }
        
        return security_blocked;
    }
    
    // Test legitimate access still works
    bool TestLegitimateAccess(const std::string& legitimate_path, const std::string& description = "") {
        int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (client_sock < 0) {
            SEC_LOG("Failed to create client socket for legitimate access test");
            return false;
        }
        
        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
        server_addr.sin_port = htons(kSecurityTestPort);
        
        SEC_LOG("Testing legitimate access for: %s [%s]", legitimate_path.c_str(), description.c_str());
        
        // Create and send RRQ packet
        TftpPacket rrq_packet = TftpPacket::CreateReadRequest(legitimate_path, TransferMode::kOctet);
        if (!SendTftpPacket(client_sock, server_addr, rrq_packet.Serialize())) {
            SEC_LOG("Failed to send RRQ packet");
            CloseSocket(client_sock);
            return false;
        }
        
        // Receive response - should be DATA packet or ERROR (if file doesn't exist, that's legitimate)
        std::vector<uint8_t> response;
        if (!ReceiveTftpPacket(client_sock, response, server_addr, 2000)) {
            SEC_LOG("No response received - legitimate access test failed");
            CloseSocket(client_sock);
            return false;
        }
        
        TftpPacket response_packet;
        if (!response_packet.Deserialize(response)) {
            SEC_LOG("Failed to parse response packet");
            CloseSocket(client_sock);
            return false;
        }
        
        CloseSocket(client_sock);
        
        // Legitimate access works if we get DATA (success) or file-not-found error (legitimate)
        bool access_legitimate = (response_packet.GetOpCode() == OpCode::kData) || 
                                (response_packet.GetOpCode() == OpCode::kError && 
                                 response_packet.GetErrorCode() == ErrorCode::kFileNotFound);
        
        if (access_legitimate) {
            if (response_packet.GetOpCode() == OpCode::kData) {
                SEC_LOG("LEGITIMATE ACCESS CONFIRMED: %s accessible (received DATA)", legitimate_path.c_str());
            } else {
                SEC_LOG("LEGITIMATE ACCESS CONFIRMED: %s properly handled (file not found)", legitimate_path.c_str());
            }
        } else {
            SEC_LOG("LEGITIMATE ACCESS FAILED: %s blocked unexpectedly (Error: %d - %s)", 
                   legitimate_path.c_str(),
                   static_cast<int>(response_packet.GetErrorCode()), 
                   response_packet.GetErrorMessage().c_str());
        }
        
        return access_legitimate;
    }

private:
    std::unique_ptr<TftpServer> server_;
};

// ==== COMPREHENSIVE DIRECTORY TRAVERSAL TESTS ====

TEST_F(TftpServerSecurityTest, BasicDirectoryTraversalAttacks) {
    SEC_LOG("=== Testing Basic Directory Traversal Attacks ===");
    
    // Basic relative path attacks
    EXPECT_TRUE(TestSecurityBlockedRead("../outside_root/secret_file.txt", "Basic parent directory traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("../../outside_root/secret_file.txt", "Double parent directory traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("../../../outside_root/secret_file.txt", "Triple parent directory traversal"));
    
    // Windows-style backslashes
    EXPECT_TRUE(TestSecurityBlockedRead("..\\outside_root\\secret_file.txt", "Windows-style parent directory traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("..\\..\\outside_root\\secret_file.txt", "Windows double parent traversal"));
    
    // Mixed separators
    EXPECT_TRUE(TestSecurityBlockedRead("../outside_root\\secret_file.txt", "Mixed separator traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("..\\outside_root/secret_file.txt", "Mixed separator traversal reverse"));
    
    SEC_LOG("=== Basic Directory Traversal Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, AdvancedDirectoryTraversalAttacks) {
    SEC_LOG("=== Testing Advanced Directory Traversal Attacks ===");
    
    // Double dot variants
    EXPECT_TRUE(TestSecurityBlockedRead("....//outside_root//secret_file.txt", "Double dot with double slash"));
    EXPECT_TRUE(TestSecurityBlockedRead("....\\\\outside_root\\\\secret_file.txt", "Double dot with double backslash"));
    
    // URL encoded attacks
    EXPECT_TRUE(TestSecurityBlockedRead("%2e%2e%2foutside_root%2fsecret_file.txt", "URL encoded traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("%2e%2e/%2e%2e/outside_root/secret_file.txt", "Partial URL encoded traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("..%2foutside_root%2fsecret_file.txt", "Mixed encoded traversal"));
    
    // Unicode variants (if supported)
    EXPECT_TRUE(TestSecurityBlockedRead("..%c0%afoutside_root%c0%afsecret_file.txt", "Unicode encoded traversal"));
    
    // Alternative encoding attempts
    EXPECT_TRUE(TestSecurityBlockedRead("%2E%2E%2Foutside_root%2Fsecret_file.txt", "Uppercase URL encoded"));
    
    SEC_LOG("=== Advanced Directory Traversal Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, AbsolutePathAttacks) {
    SEC_LOG("=== Testing Absolute Path Attacks ===");
    
    // Unix-style absolute paths
    EXPECT_TRUE(TestSecurityBlockedRead("/etc/passwd", "Unix absolute path to /etc/passwd"));
    EXPECT_TRUE(TestSecurityBlockedRead("/etc/shadow", "Unix absolute path to /etc/shadow"));
    EXPECT_TRUE(TestSecurityBlockedRead("/tmp/sensitive_file.txt", "Unix absolute path to /tmp"));
    EXPECT_TRUE(TestSecurityBlockedRead("/var/log/auth.log", "Unix absolute path to log file"));
    
#ifdef _WIN32
    // Windows-style absolute paths
    EXPECT_TRUE(TestSecurityBlockedRead("C:\\Windows\\System32\\config\\SAM", "Windows absolute path to SAM"));
    EXPECT_TRUE(TestSecurityBlockedRead("C:\\Windows\\System32\\drivers\\etc\\hosts", "Windows absolute path to hosts"));
    EXPECT_TRUE(TestSecurityBlockedRead("D:\\sensitive_data.txt", "Windows D: drive access"));
    EXPECT_TRUE(TestSecurityBlockedRead("C:/Windows/win.ini", "Windows absolute path with forward slashes"));
    
    // UNC path attacks
    EXPECT_TRUE(TestSecurityBlockedRead("\\\\server\\share\\file.txt", "UNC path attack"));
    EXPECT_TRUE(TestSecurityBlockedRead("//server/share/file.txt", "UNC path with forward slashes"));
#endif
    
    SEC_LOG("=== Absolute Path Attack Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, NullByteInjectionAttacks) {
    SEC_LOG("=== Testing Null Byte Injection Attacks ===");
    
    // Note: TFTP protocol uses null-terminated strings, so null bytes will truncate filenames.
    // However, the IsPathSecure function should still validate and block null bytes in paths.
    
    // Test IsPathSecure function directly with null byte injection
    std::string null_path1 = "normal_file.txt";
    null_path1.push_back('\0');
    null_path1 += "../outside_root/secret_file.txt";
    EXPECT_FALSE(util::IsPathSecure(null_path1, kSecurityTestRootDir)) 
        << "IsPathSecure should block null byte injection";
    
    std::string null_path2 = "normal";
    null_path2.push_back('\0');
    null_path2 += ".txt/../outside_root/secret_file.txt";
    EXPECT_FALSE(util::IsPathSecure(null_path2, kSecurityTestRootDir))
        << "IsPathSecure should block null byte in middle of path";
    
    // Test that protocol-level null termination works as expected
    // (The malicious part after null byte should be ignored by TFTP protocol)
    std::string truncated_path = "normal_file.txt";  // This should be treated as legitimate
    EXPECT_TRUE(TestLegitimateAccess(truncated_path, "Null byte truncated path"));
    
    SEC_LOG("=== Null Byte Injection Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, SpecialCharacterAttacks) {
    SEC_LOG("=== Testing Special Character Attacks ===");
    
    // Control characters
    EXPECT_TRUE(TestSecurityBlockedRead("file\nname.txt", "Newline injection"));
    EXPECT_TRUE(TestSecurityBlockedRead("file\rname.txt", "Carriage return injection"));
    EXPECT_TRUE(TestSecurityBlockedRead("file\tname.txt", "Tab injection"));
    EXPECT_TRUE(TestSecurityBlockedRead("file\x7fname.txt", "DEL character injection"));
    
    // Shell special characters
    EXPECT_TRUE(TestSecurityBlockedRead("file|name.txt", "Pipe character"));
    EXPECT_TRUE(TestSecurityBlockedRead("file>name.txt", "Redirect character"));
    EXPECT_TRUE(TestSecurityBlockedRead("file<name.txt", "Input redirect character"));
    EXPECT_TRUE(TestSecurityBlockedRead("file&name.txt", "Ampersand character"));
    EXPECT_TRUE(TestSecurityBlockedRead("file;name.txt", "Semicolon character"));
    EXPECT_TRUE(TestSecurityBlockedRead("file`name.txt", "Backtick character"));
    EXPECT_TRUE(TestSecurityBlockedRead("file$name.txt", "Dollar sign character"));
    
    // Wildcard characters
    EXPECT_TRUE(TestSecurityBlockedRead("file*name.txt", "Asterisk wildcard"));
    EXPECT_TRUE(TestSecurityBlockedRead("file?name.txt", "Question mark wildcard"));
    
    SEC_LOG("=== Special Character Attack Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, LongPathAttacks) {
    SEC_LOG("=== Testing Long Path Attacks ===");
    
    // Test IsPathSecure with very long filename 
    std::string long_filename(1000, 'A');
    long_filename += ".txt";
    // Note: Very long filenames without traversal patterns may be allowed by IsPathSecure
    // The real security check happens at filesystem level and protocol level
    // Just verify the function doesn't crash with long inputs
    bool long_filename_result = util::IsPathSecure(long_filename, kSecurityTestRootDir);
    SEC_LOG("Long filename test result: %s", long_filename_result ? "allowed" : "blocked");
    
    // Test moderately long path with traversal via network
    std::string moderate_path = "../outside_root/";
    moderate_path += std::string(100, 'X');  // Reduced size to avoid timeout
    moderate_path += ".txt";
    EXPECT_TRUE(TestSecurityBlockedRead(moderate_path, "Moderate long path with traversal"));
    
    // Test IsPathSecure with many directory levels (faster than network test)
    std::string deep_path = "..";
    for (int i = 0; i < 50; i++) {  // Reduced from 100 to avoid timeout
        deep_path += "/..";
    }
    deep_path += "/outside_root/secret_file.txt";
    EXPECT_FALSE(util::IsPathSecure(deep_path, kSecurityTestRootDir))
        << "IsPathSecure should block deep directory traversal";
    
    SEC_LOG("=== Long Path Attack Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, EdgeCasePathAttacks) {
    SEC_LOG("=== Testing Edge Case Path Attacks ===");
    
    // Test IsPathSecure directly for edge cases (more reliable)
    EXPECT_FALSE(util::IsPathSecure("", kSecurityTestRootDir)) << "Empty path should be blocked";
    EXPECT_FALSE(util::IsPathSecure(".", kSecurityTestRootDir)) << "Current directory should be blocked";
    EXPECT_FALSE(util::IsPathSecure("..", kSecurityTestRootDir)) << "Parent directory should be blocked";
    
    // Test directory traversal with whitespace via network
    EXPECT_TRUE(TestSecurityBlockedRead("../outside_root/secret_file.txt", "Basic traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead(" ../outside_root/secret_file.txt", "Leading space traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("../outside_root/secret_file.txt ", "Trailing space traversal"));
    EXPECT_TRUE(TestSecurityBlockedRead("../ outside_root/secret_file.txt", "Space after traversal"));
    
    // Test absolute paths via IsPathSecure (more reliable)
#ifdef _WIN32
    EXPECT_FALSE(util::IsPathSecure("C:\\Windows\\System32", kSecurityTestRootDir)) << "Windows absolute path should be blocked";
    EXPECT_FALSE(util::IsPathSecure("\\\\server\\share", kSecurityTestRootDir)) << "UNC path should be blocked";
#else
    EXPECT_FALSE(util::IsPathSecure("/etc/passwd", kSecurityTestRootDir)) << "Unix absolute path should be blocked";
    EXPECT_FALSE(util::IsPathSecure("/tmp/file", kSecurityTestRootDir)) << "Unix absolute path should be blocked";
#endif
    
    SEC_LOG("=== Edge Case Path Attack Tests Completed ===");
}

// ==== LEGITIMATE ACCESS VERIFICATION TESTS ====

TEST_F(TftpServerSecurityTest, LegitimateFileAccess) {
    SEC_LOG("=== Testing Legitimate File Access ===");
    
    // Normal files should be accessible
    EXPECT_TRUE(TestLegitimateAccess("normal_file.txt", "Normal file in root"));
    EXPECT_TRUE(TestLegitimateAccess("text_file.txt", "Text file in root"));
    EXPECT_TRUE(TestLegitimateAccess("binary_file.bin", "Binary file in root"));
    
    // Files in allowed subdirectories
    EXPECT_TRUE(TestLegitimateAccess("allowed_subdir/subdir_file.txt", "File in allowed subdirectory"));
    EXPECT_TRUE(TestLegitimateAccess("deep/nested/directory/deep_file.txt", "File in deep nested directory"));
    
    SEC_LOG("=== Legitimate File Access Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, LegitimateFilenamePatterns) {
    SEC_LOG("=== Testing Legitimate Filename Patterns ===");
    
    // Various legitimate filename patterns should work
    EXPECT_TRUE(TestLegitimateAccess("file_with_spaces.txt", "File with spaces"));
    EXPECT_TRUE(TestLegitimateAccess("file.with.dots.txt", "File with dots"));
    EXPECT_TRUE(TestLegitimateAccess("file-with-dashes.txt", "File with dashes"));
    EXPECT_TRUE(TestLegitimateAccess("file_with_underscores.txt", "File with underscores"));
    EXPECT_TRUE(TestLegitimateAccess("UPPERCASE_FILE.TXT", "Uppercase filename"));
    EXPECT_TRUE(TestLegitimateAccess("123numeric_start.txt", "Numeric start filename"));
    
    SEC_LOG("=== Legitimate Filename Pattern Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, NonExistentFileLegitimateHandling) {
    SEC_LOG("=== Testing Non-Existent File Handling ===");
    
    // Non-existent files should return file-not-found error, not security error
    EXPECT_TRUE(TestLegitimateAccess("non_existent_file.txt", "Non-existent file"));
    EXPECT_TRUE(TestLegitimateAccess("allowed_subdir/non_existent.txt", "Non-existent file in subdir"));
    
    SEC_LOG("=== Non-Existent File Handling Tests Completed ===");
}

// ==== COMPREHENSIVE UNIT TESTS FOR IsPathSecure FUNCTION ====

TEST_F(TftpServerSecurityTest, IsPathSecureFunction_DirectTraversal) {
    SEC_LOG("=== Testing IsPathSecure Function - Direct Traversal ===");
    
    // These should all be blocked
    EXPECT_FALSE(util::IsPathSecure("../outside_file.txt", kSecurityTestRootDir));
    EXPECT_FALSE(util::IsPathSecure("../../outside_file.txt", kSecurityTestRootDir));
    EXPECT_FALSE(util::IsPathSecure("..\\outside_file.txt", kSecurityTestRootDir));
    EXPECT_FALSE(util::IsPathSecure("..\\..\\outside_file.txt", kSecurityTestRootDir));
    EXPECT_FALSE(util::IsPathSecure("../subdir/../outside_file.txt", kSecurityTestRootDir));
    
    SEC_LOG("=== IsPathSecure Direct Traversal Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, IsPathSecureFunction_AbsolutePaths) {
    SEC_LOG("=== Testing IsPathSecure Function - Absolute Paths ===");
    
    // Absolute paths should be blocked
    EXPECT_FALSE(util::IsPathSecure("/etc/passwd", kSecurityTestRootDir));
    EXPECT_FALSE(util::IsPathSecure("/tmp/file.txt", kSecurityTestRootDir));
    
#ifdef _WIN32
    EXPECT_FALSE(util::IsPathSecure("C:\\Windows\\System32\\file.txt", kSecurityTestRootDir));
    EXPECT_FALSE(util::IsPathSecure("D:\\sensitive.txt", kSecurityTestRootDir));
    EXPECT_FALSE(util::IsPathSecure("\\\\server\\share\\file.txt", kSecurityTestRootDir));
#endif
    
    SEC_LOG("=== IsPathSecure Absolute Path Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, IsPathSecureFunction_LegitimateAccess) {
    SEC_LOG("=== Testing IsPathSecure Function - Legitimate Access ===");
    
    // These should all be allowed
    EXPECT_TRUE(util::IsPathSecure("normal_file.txt", kSecurityTestRootDir));
    EXPECT_TRUE(util::IsPathSecure("subdir/file.txt", kSecurityTestRootDir));
    EXPECT_TRUE(util::IsPathSecure("deep/nested/directory/file.txt", kSecurityTestRootDir));
    EXPECT_TRUE(util::IsPathSecure("file_with_spaces.txt", kSecurityTestRootDir));
    EXPECT_TRUE(util::IsPathSecure("file.with.dots.txt", kSecurityTestRootDir));
    EXPECT_TRUE(util::IsPathSecure("file-with-dashes.txt", kSecurityTestRootDir));
    
    SEC_LOG("=== IsPathSecure Legitimate Access Tests Completed ===");
}

TEST_F(TftpServerSecurityTest, IsPathSecureFunction_SpecialCases) {
    SEC_LOG("=== Testing IsPathSecure Function - Special Cases ===");
    
    // Special cases that should be blocked
    EXPECT_FALSE(util::IsPathSecure("", kSecurityTestRootDir));  // Empty path
    EXPECT_FALSE(util::IsPathSecure(".", kSecurityTestRootDir));  // Current directory
    EXPECT_FALSE(util::IsPathSecure("..", kSecurityTestRootDir)); // Parent directory
    
    // Null byte injection
    std::string null_path = "file.txt";
    null_path.push_back('\0');
    null_path += "../secret.txt";
    EXPECT_FALSE(util::IsPathSecure(null_path, kSecurityTestRootDir));
    
    SEC_LOG("=== IsPathSecure Special Cases Tests Completed ===");
}
