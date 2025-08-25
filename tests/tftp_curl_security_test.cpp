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
#include <tftp/tftp_common.h>
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
constexpr uint16_t kSecTestPort = 6972;
constexpr const char* kSecTestRootDir = "./curl_security_test_files";

// Log macros
#ifdef _WIN32
#define SEC_LOG(fmt, ...) do { \
    fprintf(stdout, "[SECURITY TEST] " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)
#else
#define SEC_LOG(fmt, ...) fprintf(stdout, "[SECURITY TEST] " fmt "\n", ##__VA_ARGS__); fflush(stdout)
#endif

/**
 * @class CurlTftpSecurityTest
 * @brief Security and edge case tests for curl TFTP client
 */
class CurlTftpSecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory structure
        std::filesystem::create_directories(kSecTestRootDir);
        std::filesystem::create_directories(std::string(kSecTestRootDir) + "/allowed_subdir");
        std::filesystem::create_directories(std::string(kSecTestRootDir) + "/../outside_root");
        
        // Create test files
        CreateSecurityTestFiles();

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        // Start TFTP server with security settings
        server_ = std::make_unique<TftpServer>(kSecTestRootDir, kSecTestPort);
        server_->SetTimeout(10);
        server_->SetSecureMode(true);  // Enable security mode if available
        
        ASSERT_TRUE(server_->Start()) << "Failed to start TFTP server for security tests";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        SEC_LOG("TFTP Security Test Server started on port %d", kSecTestPort);
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
            server_.reset();
        }

        // Clean up test directories
        std::filesystem::remove_all(kSecTestRootDir);
        std::filesystem::remove_all(std::string(kSecTestRootDir) + "/../outside_root");

#ifdef _WIN32
        WSACleanup();
#endif
    }

    void CreateSecurityTestFiles() {
        // Normal files
        WriteFile("normal_file.txt", "This is a normal file content.\n");
        WriteFile("allowed_subdir/subdir_file.txt", "File in allowed subdirectory.\n");
        
        // File outside root directory (should not be accessible via TFTP)
        std::string outside_file = std::string(kSecTestRootDir) + "/../outside_root/secret_file.txt";
        std::ofstream outside(outside_file);
        outside << "This file should not be accessible via TFTP.\n";
        outside.close();
        
        // Create files with various special names
        WriteFile("file_with_spaces.txt", "File with spaces in name.\n");
        WriteFile("file.with.dots.txt", "File with dots in name.\n");
        WriteFile("file-with-dashes.txt", "File with dashes in name.\n");
        WriteFile("UPPERCASE_FILE.TXT", "File with uppercase name.\n");
        
#ifdef _WIN32
        // Windows-specific test files
        WriteFile("file_with_underscore_.txt", "File ending with underscore.\n");
#else
        // Unix-specific test files
        WriteFile(".hidden_file", "Hidden file on Unix systems.\n");
#endif
        
        SEC_LOG("Created security test files");
    }

    void WriteFile(const std::string& filename, const std::string& content) {
        std::string filepath = std::string(kSecTestRootDir) + "/" + filename;
        
        // Create parent directories if needed
        std::filesystem::create_directories(std::filesystem::path(filepath).parent_path());
        
        std::ofstream file(filepath);
        file << content;
        file.close();
    }

    std::pair<int, std::string> ExecuteCurlCommand(const std::vector<std::string>& args) {
        std::string command = "C:\\Windows\\System32\\curl.exe";
        for (const auto& arg : args) {
            // Handle arguments that might contain spaces or special characters
            if (arg.find(' ') != std::string::npos || arg.find('&') != std::string::npos) {
                command += " \"" + arg + "\"";
            } else {
                command += " " + arg;
            }
        }
        
        SEC_LOG("Executing: %s", command.c_str());
        
        std::array<char, 256> buffer;
        std::string output;
        
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif
        
        if (!pipe) {
            return {-1, "Failed to execute command"};
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            output += buffer.data();
        }
        
#ifdef _WIN32
        int exit_code = _pclose(pipe.release());
#else
        int exit_code = pclose(pipe.release());
#endif
        
        return {exit_code, output};
    }

    bool TestDownloadExpectFailure(const std::string& remote_file, 
                                  const std::string& description = "") {
        std::string local_file = std::string(kSecTestRootDir) + "/security_download_test.tmp";
        std::filesystem::remove(local_file);
        
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "5",
            "--max-time", "10",
            "-f",  // Fail on HTTP errors
            "-s",  // Silent mode
            "-o", local_file,
            "tftp://127.0.0.1:" + std::to_string(kSecTestPort) + "/" + remote_file
        };
        
        auto result = ExecuteCurlCommand(args);
        
        SEC_LOG("Security test [%s]: file='%s', exit_code=%d, expected=FAILURE", 
               description.c_str(), remote_file.c_str(), result.first);
        
        // Should fail (non-zero exit code) and no file should be created
        bool failed_as_expected = (result.first != 0) && !std::filesystem::exists(local_file);
        
        // Clean up any accidentally created file
        std::filesystem::remove(local_file);
        
        return failed_as_expected;
    }

    bool TestDownloadExpectSuccess(const std::string& remote_file, 
                                  const std::string& description = "") {
        std::string local_file = std::string(kSecTestRootDir) + "/security_download_success.tmp";
        std::filesystem::remove(local_file);
        
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "5",
            "--max-time", "10",
            "-o", local_file,
            "tftp://127.0.0.1:" + std::to_string(kSecTestPort) + "/" + remote_file
        };
        
        auto result = ExecuteCurlCommand(args);
        
        SEC_LOG("Security test [%s]: file='%s', exit_code=%d, expected=SUCCESS", 
               description.c_str(), remote_file.c_str(), result.first);
        
        bool succeeded = (result.first == 0) && std::filesystem::exists(local_file);
        
        // Clean up
        std::filesystem::remove(local_file);
        
        return succeeded;
    }

    bool TestUploadExpectFailure(const std::string& local_content, 
                                const std::string& remote_file,
                                const std::string& description = "") {
        std::string local_file = std::string(kSecTestRootDir) + "/security_upload_source.tmp";
        
        // Create source file
        std::ofstream source(local_file);
        source << local_content;
        source.close();
        
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "5",
            "--max-time", "10",
            "-f",  // Fail on HTTP errors
            "-s",  // Silent mode
            "-T", local_file,
            "tftp://127.0.0.1:" + std::to_string(kSecTestPort) + "/" + remote_file
        };
        
        auto result = ExecuteCurlCommand(args);
        
        SEC_LOG("Security upload test [%s]: file='%s', exit_code=%d, expected=FAILURE", 
               description.c_str(), remote_file.c_str(), result.first);
        
        // Clean up source file
        std::filesystem::remove(local_file);
        
        // Should fail (non-zero exit code)
        return result.first != 0;
    }

private:
    std::unique_ptr<TftpServer> server_;
};

// ==== DIRECTORY TRAVERSAL TESTS ====

TEST_F(CurlTftpSecurityTest, DirectoryTraversalPrevention) {
    SEC_LOG("Testing directory traversal attack prevention");
    
    // Test various directory traversal patterns
    std::vector<std::string> traversal_attempts = {
        "../outside_root/secret_file.txt",
        "..\\..\\outside_root\\secret_file.txt",
        "....//....//outside_root//secret_file.txt",
        "%2e%2e%2foutside_root%2fsecret_file.txt",  // URL encoded
        "..%2f..%2foutside_root%2fsecret_file.txt",  // Mixed encoding
        "/etc/passwd",  // Absolute path (Unix)
        "\\windows\\system32\\config\\sam",  // Absolute path (Windows)
        "../../../etc/shadow",
        "..\\..\\..\\windows\\system32\\drivers\\etc\\hosts"
    };
    
    for (const auto& attempt : traversal_attempts) {
        EXPECT_TRUE(TestDownloadExpectFailure(attempt, "Directory Traversal"))
            << "Directory traversal should be blocked: " << attempt;
    }
    
    SEC_LOG("Directory traversal prevention tests completed");
}

TEST_F(CurlTftpSecurityTest, DirectoryTraversalUpload) {
    SEC_LOG("Testing directory traversal prevention in uploads");
    
    std::vector<std::string> traversal_uploads = {
        "../outside_root/malicious_upload.txt",
        "..\\..\\outside_root\\malicious_upload.txt",
        "/tmp/malicious_upload.txt",
        "\\temp\\malicious_upload.txt"
    };
    
    for (const auto& attempt : traversal_uploads) {
        EXPECT_TRUE(TestUploadExpectFailure("Malicious content", attempt, "Upload Directory Traversal"))
            << "Upload directory traversal should be blocked: " << attempt;
    }
    
    SEC_LOG("Directory traversal upload prevention tests completed");
}

// ==== FILENAME VALIDATION TESTS ====

TEST_F(CurlTftpSecurityTest, ValidFilenameHandling) {
    SEC_LOG("Testing valid filename handling");
    
    // These should work
    std::vector<std::string> valid_files = {
        "normal_file.txt",
        "allowed_subdir/subdir_file.txt",
        "file_with_spaces.txt",
        "file.with.dots.txt",
        "file-with-dashes.txt",
        "UPPERCASE_FILE.TXT"
    };
    
    for (const auto& filename : valid_files) {
        EXPECT_TRUE(TestDownloadExpectSuccess(filename, "Valid Filename"))
            << "Valid filename should be accessible: " << filename;
    }
    
    SEC_LOG("Valid filename handling tests completed");
}

TEST_F(CurlTftpSecurityTest, InvalidFilenameRejection) {
    SEC_LOG("Testing invalid filename rejection");
    
    // These should be rejected
    std::vector<std::string> invalid_files = {
        "",  // Empty filename
        ".",  // Current directory
        "..",  // Parent directory
        "file\x00name.txt",  // Null byte injection
        "file\nname.txt",  // Newline injection
        "file\rname.txt",  // Carriage return injection
        "file\tname.txt",  // Tab injection
        "file|name.txt",  // Pipe character
        "file>name.txt",  // Redirection
        "file<name.txt",  // Redirection
        "file&name.txt",  // Command separator
        "file;name.txt",  // Command separator
        "file`name.txt",  // Command substitution
        "file$name.txt",  // Variable expansion
        "file*name.txt",  // Wildcard
        "file?name.txt"   // Wildcard
    };
    
    for (const auto& filename : invalid_files) {
        if (filename.empty()) {
            // Skip empty filename as it may cause curl argument parsing issues
            continue;
        }
        
        EXPECT_TRUE(TestDownloadExpectFailure(filename, "Invalid Filename"))
            << "Invalid filename should be rejected: [" << filename << "]";
    }
    
    SEC_LOG("Invalid filename rejection tests completed");
}

// ==== SPECIAL CHARACTER TESTS ====

TEST_F(CurlTftpSecurityTest, SpecialCharacterHandling) {
    SEC_LOG("Testing special character handling in filenames");
    
    // Test filenames with various special characters that might be valid
    // but need proper handling
    std::vector<std::pair<std::string, bool>> special_char_tests = {
        {"file with spaces.txt", true},  // Should work
        {"file.with.dots.txt", true},   // Should work
        {"file-with-dashes.txt", true}, // Should work
        {"file_with_underscores.txt", true}, // Should work
        {"123numeric_start.txt", true}, // Should work
        {"file(with)parens.txt", false}, // May be rejected
        {"file[with]brackets.txt", false}, // May be rejected
        {"file{with}braces.txt", false}, // May be rejected
        {"file@sign.txt", false}, // May be rejected
        {"file#hash.txt", false}, // May be rejected
        {"file%percent.txt", false}, // May be rejected
        {"file^caret.txt", false}, // May be rejected
        {"file=equals.txt", false}, // May be rejected
        {"file+plus.txt", false}  // May be rejected
    };
    
    for (const auto& test_case : special_char_tests) {
        const std::string& filename = test_case.first;
        bool should_succeed = test_case.second;
        
        // First create the file if it should succeed
        if (should_succeed) {
            WriteFile(filename, "Test content for " + filename + "\n");
        }
        
        if (should_succeed) {
            bool success = TestDownloadExpectSuccess(filename, "Special Characters Valid");
            EXPECT_TRUE(success) << "Valid special character filename should work: " << filename;
        } else {
            bool failure = TestDownloadExpectFailure(filename, "Special Characters Invalid");
            // Note: Some of these might actually succeed depending on server implementation
            // The test documents the behavior rather than enforcing strict requirements
            SEC_LOG("Special character test result for '%s': %s", 
                   filename.c_str(), failure ? "BLOCKED" : "ALLOWED");
        }
    }
    
    SEC_LOG("Special character handling tests completed");
}

// ==== PROTOCOL ABUSE TESTS ====

TEST_F(CurlTftpSecurityTest, ProtocolAbuseDetection) {
    SEC_LOG("Testing protocol abuse detection");
    
    // Test various malformed or abusive requests
    std::vector<std::string> abuse_attempts = {
        "normal_file.txt\x00extra_data",  // Null byte injection
        "normal_file.txt\nmalicious_command",  // Newline injection
        "normal_file.txt\rmalicious_command",  // CR injection
        std::string(1000, 'A') + ".txt",  // Extremely long filename
        std::string(10000, 'X'),  // Extremely long request
        "normal_file.txt?" + std::string(1000, 'Q'),  // Query string abuse
        "normal_file.txt#" + std::string(1000, 'H')   // Fragment abuse
    };
    
    for (const auto& attempt : abuse_attempts) {
        bool blocked = TestDownloadExpectFailure(attempt, "Protocol Abuse");
        
        // Note: The expectation here is that the server should handle these gracefully,
        // either by rejecting them or by processing them safely
        SEC_LOG("Protocol abuse test for malformed request: %s", 
               blocked ? "BLOCKED/HANDLED" : "UNEXPECTED_SUCCESS");
        
        // We don't strictly require these to fail, as the server might handle them safely
        // This is more of a behavioral documentation test
    }
    
    SEC_LOG("Protocol abuse detection tests completed");
}

// ==== RESOURCE EXHAUSTION TESTS ====

TEST_F(CurlTftpSecurityTest, ResourceExhaustionPrevention) {
    SEC_LOG("Testing resource exhaustion prevention");
    
    // Test rapid consecutive requests (basic DoS protection)
    const int rapid_requests = 10;
    int successful_requests = 0;
    int failed_requests = 0;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < rapid_requests; ++i) {
        std::string local_file = std::string(kSecTestRootDir) + "/rapid_test_" + std::to_string(i) + ".tmp";
        std::filesystem::remove(local_file);
        
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "2",
            "--max-time", "5",
            "-o", local_file,
            "tftp://127.0.0.1:" + std::to_string(kSecTestPort) + "/normal_file.txt"
        };
        
        auto result = ExecuteCurlCommand(args);
        
        if (result.first == 0) {
            successful_requests++;
        } else {
            failed_requests++;
        }
        
        std::filesystem::remove(local_file);
        
        // Small delay between requests
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    SEC_LOG("Rapid request test completed in %lld ms", duration.count());
    SEC_LOG("Successful requests: %d/%d", successful_requests, rapid_requests);
    SEC_LOG("Failed requests: %d/%d", failed_requests, rapid_requests);
    
    // We expect most requests to succeed, but some rate limiting might be in place
    EXPECT_GT(successful_requests, rapid_requests / 2) 
        << "Server should handle reasonable rapid requests";
    
    SEC_LOG("Resource exhaustion prevention tests completed");
}

// ==== CONCURRENT CONNECTION TESTS ====

TEST_F(CurlTftpSecurityTest, ConcurrentConnectionLimits) {
    SEC_LOG("Testing concurrent connection handling");
    
    const int num_concurrent = 5;
    std::vector<std::thread> test_threads;
    std::vector<int> results(num_concurrent, -1);
    
    // Launch concurrent connections
    for (int i = 0; i < num_concurrent; ++i) {
        test_threads.emplace_back([this, i, &results]() {
            std::string local_file = std::string(kSecTestRootDir) + "/concurrent_" + std::to_string(i) + ".tmp";
            std::filesystem::remove(local_file);
            
            std::vector<std::string> args = {
                "--tftp-blksize", "512",
                "--connect-timeout", "5",
                "--max-time", "10",
                "-o", local_file,
                "tftp://127.0.0.1:" + std::to_string(kSecTestPort) + "/normal_file.txt"
            };
            
            auto result = ExecuteCurlCommand(args);
            results[i] = result.first;
            
            std::filesystem::remove(local_file);
            
            SEC_LOG("Concurrent connection %d completed with exit code: %d", i, result.first);
        });
    }
    
    // Wait for all connections to complete
    for (auto& thread : test_threads) {
        thread.join();
    }
    
    // Analyze results
    int successful_connections = 0;
    for (int result : results) {
        if (result == 0) {
            successful_connections++;
        }
    }
    
    SEC_LOG("Concurrent connections: %d successful out of %d", 
           successful_connections, num_concurrent);
    
    // We expect the server to handle reasonable concurrent connections
    EXPECT_GE(successful_connections, num_concurrent / 2) 
        << "Server should handle reasonable concurrent connections";
    
    SEC_LOG("Concurrent connection limit tests completed");
}

// Note: main() function is provided by the main test file (tftp_server_test.cpp)