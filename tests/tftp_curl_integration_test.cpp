#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Windows header fix for proper inclusion order
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
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <memory>
#include <array>

using namespace tftpserver;

// Log macros for Windows/Unix
#ifdef _WIN32
#define LOG_INFO(fmt, ...) do { \
    fprintf(stdout, "[CURL TEST INFO] " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
    char buf[1024]; \
    snprintf(buf, sizeof(buf), "[CURL TEST INFO] " fmt, ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    OutputDebugStringA("\n"); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    fprintf(stderr, "[CURL TEST ERROR] " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
    char buf[1024]; \
    snprintf(buf, sizeof(buf), "[CURL TEST ERROR] " fmt, ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    OutputDebugStringA("\n"); \
} while(0)
#else
#define LOG_INFO(fmt, ...) fprintf(stdout, "[CURL TEST INFO] " fmt "\n", ##__VA_ARGS__); fflush(stdout)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[CURL TEST ERROR] " fmt "\n", ##__VA_ARGS__); fflush(stderr)
#endif

// Test constants
constexpr uint16_t kCurlTestPort = 6970;  // Different port from other tests
constexpr const char* kCurlTestRootDir = "./curl_test_files";

// Test file contents with different characteristics
constexpr const char* kSmallTextContent = "Hello TFTP World!\nThis is a small test file.\n";
constexpr const char* kMediumTextContent = 
    "This is a medium-sized test file for TFTP curl testing.\n"
    "It contains multiple lines of text to test text transfer capabilities.\n"
    "Line 3: Testing special characters: !@#$%^&*()_+-={}[]|\\:;\"'<>?,./\n"
    "Line 4: Testing UTF-8 characters if supported.\n"
    "Line 5: This line is specifically designed to be longer than usual to test line wrapping and handling.\n"
    "Line 6: Final line of medium test content.\n";

// Binary test data patterns
constexpr size_t kSmallBinarySize = 256;
constexpr size_t kMediumBinarySize = 1024 * 4;  // 4KB
constexpr size_t kLargeBinarySize = 1024 * 1024;  // 1MB

/**
 * @class CurlTftpTest
 * @brief Test class for curl-based TFTP client integration tests
 */
class CurlTftpTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        std::filesystem::create_directories(kCurlTestRootDir);
        
        // Create test files
        CreateTestFiles();

#ifdef _WIN32
        // Initialize Winsock for Windows
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        // Start TFTP server
        server_ = std::make_unique<TftpServer>(kCurlTestRootDir, kCurlTestPort);
        server_->SetTimeout(10);  // 10 second timeout for tests
        ASSERT_TRUE(server_->Start()) << "Failed to start TFTP server";
        
        // Wait for server to fully start
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        LOG_INFO("TFTP Server started on port %d with root directory: %s", 
                kCurlTestPort, kCurlTestRootDir);
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
            server_.reset();
        }

        // Clean up test directory
        std::filesystem::remove_all(kCurlTestRootDir);

#ifdef _WIN32
        WSACleanup();
#endif
    }

    /**
     * @brief Create various test files for comprehensive testing
     */
    void CreateTestFiles() {
        // Small text file
        WriteTextFile("small_text.txt", kSmallTextContent);
        
        // Medium text file
        WriteTextFile("medium_text.txt", kMediumTextContent);
        
        // Large text file (repeated content)
        std::string large_text;
        for (int i = 0; i < 100; ++i) {
            large_text += "Line " + std::to_string(i) + ": " + kMediumTextContent;
        }
        WriteTextFile("large_text.txt", large_text);
        
        // Binary files with different sizes
        CreateBinaryFile("small_binary.bin", kSmallBinarySize);
        CreateBinaryFile("medium_binary.bin", kMediumBinarySize);
        CreateBinaryFile("large_binary.bin", kLargeBinarySize);
        
        // Empty file
        WriteTextFile("empty_file.txt", "");
        
        // File with special name characters
        WriteTextFile("test_file_with_underscores.txt", "File with underscores in name.\n");
        
        LOG_INFO("Created test files in directory: %s", kCurlTestRootDir);
    }

    /**
     * @brief Helper function to write text files
     */
    void WriteTextFile(const std::string& filename, const std::string& content) {
        std::string filepath = std::string(kCurlTestRootDir) + "/" + filename;
        std::ofstream file(filepath, std::ios::binary);
        ASSERT_TRUE(file.good()) << "Failed to create file: " << filepath;
        file.write(content.data(), content.size());
        file.close();
    }

    /**
     * @brief Helper function to create binary test files with patterns
     */
    void CreateBinaryFile(const std::string& filename, size_t size) {
        std::string filepath = std::string(kCurlTestRootDir) + "/" + filename;
        std::ofstream file(filepath, std::ios::binary);
        ASSERT_TRUE(file.good()) << "Failed to create binary file: " << filepath;
        
        // Create deterministic binary pattern for verification
        std::vector<uint8_t> pattern(size);
        for (size_t i = 0; i < size; ++i) {
            pattern[i] = static_cast<uint8_t>((i * 37 + 23) % 256);  // Deterministic pattern
        }
        
        file.write(reinterpret_cast<const char*>(pattern.data()), static_cast<std::streamsize>(size));
        file.close();
    }

    /**
     * @brief Execute curl command and capture output
     * @param args curl command arguments
     * @param working_dir Working directory for curl execution
     * @return pair of <exit_code, output_string>
     */
    std::pair<int, std::string> ExecuteCurlCommand(const std::vector<std::string>& args, 
                                                   const std::string& working_dir = "") {
        std::string command = "C:\\Windows\\System32\\curl.exe";
        for (const auto& arg : args) {
            command += " \"" + arg + "\"";
        }
        
        LOG_INFO("Executing curl command: %s", command.c_str());
        
        // Change to working directory if specified
        std::string original_dir;
        if (!working_dir.empty()) {
            original_dir = std::filesystem::current_path().string();
            std::filesystem::current_path(working_dir);
        }
        
        // Execute command and capture output
        std::array<char, 128> buffer;
        std::string output;
        int exit_code = 0;
        
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif
        
        if (!pipe) {
            LOG_ERROR("Failed to execute curl command");
            return {-1, "Failed to execute command"};
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            output += buffer.data();
        }
        
        // Get exit code
#ifdef _WIN32
        exit_code = _pclose(pipe.release());
#else
        exit_code = pclose(pipe.release());
#endif
        
        // Restore original directory
        if (!working_dir.empty() && !original_dir.empty()) {
            std::filesystem::current_path(original_dir);
        }
        
        LOG_INFO("Curl command completed with exit code: %d", exit_code);
        if (!output.empty()) {
            LOG_INFO("Curl output: %s", output.c_str());
        }
        
        return {exit_code, output};
    }

    /**
     * @brief Upload file using curl TFTP client
     * @param local_file Local file path
     * @param remote_file Remote file name on server
     * @param mode Transfer mode ("binary" or "ascii")
     * @return true if successful
     */
    bool CurlUploadFile(const std::string& local_file, const std::string& remote_file, 
                        const std::string& mode = "binary") {
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "10",
            "--max-time", "30",
            "-T", local_file,
            "tftp://127.0.0.1:" + std::to_string(kCurlTestPort) + "/" + remote_file
        };
        
        // Add mode-specific options
        if (mode == "ascii") {
            args.insert(args.begin(), "--tftp-no-options");
        }
        
        auto result = ExecuteCurlCommand(args);
        return result.first == 0;
    }

    /**
     * @brief Download file using curl TFTP client
     * @param remote_file Remote file name on server
     * @param local_file Local file path for download
     * @param mode Transfer mode ("binary" or "ascii")
     * @return true if successful
     */
    bool CurlDownloadFile(const std::string& remote_file, const std::string& local_file,
                          const std::string& mode = "binary") {
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "10",
            "--max-time", "30",
            "-o", local_file,
            "tftp://127.0.0.1:" + std::to_string(kCurlTestPort) + "/" + remote_file
        };
        
        // Add mode-specific options
        if (mode == "ascii") {
            args.insert(args.begin(), "--tftp-no-options");
        }
        
        auto result = ExecuteCurlCommand(args);
        return result.first == 0;
    }

    /**
     * @brief Test download with expected failure
     * @param remote_file Remote file name
     * @param expected_error_pattern Expected error pattern in curl output
     * @return true if curl failed as expected with matching error
     */
    bool CurlDownloadExpectFailure(const std::string& remote_file, 
                                   const std::string& expected_error_pattern = "") {
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "5",
            "--max-time", "10",
            "-f",  // Fail silently on HTTP errors
            "tftp://127.0.0.1:" + std::to_string(kCurlTestPort) + "/" + remote_file
        };
        
        auto result = ExecuteCurlCommand(args);
        bool failed_as_expected = result.first != 0;
        
        if (failed_as_expected && !expected_error_pattern.empty()) {
            return result.second.find(expected_error_pattern) != std::string::npos;
        }
        
        return failed_as_expected;
    }

    /**
     * @brief Compare two files for equality
     * @param file1 First file path
     * @param file2 Second file path
     * @return true if files are identical
     */
    bool CompareFiles(const std::string& file1, const std::string& file2) {
        std::ifstream f1(file1, std::ios::binary);
        std::ifstream f2(file2, std::ios::binary);
        
        if (!f1.good() || !f2.good()) {
            LOG_ERROR("Failed to open files for comparison: %s, %s", 
                     file1.c_str(), file2.c_str());
            return false;
        }
        
        // Compare file sizes first
        f1.seekg(0, std::ios::end);
        f2.seekg(0, std::ios::end);
        if (f1.tellg() != f2.tellg()) {
            LOG_ERROR("File size mismatch: %s=%lld, %s=%lld", 
                     file1.c_str(), (long long)f1.tellg(),
                     file2.c_str(), (long long)f2.tellg());
            return false;
        }
        
        f1.seekg(0, std::ios::beg);
        f2.seekg(0, std::ios::beg);
        
        // Compare content
        return std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
                         std::istreambuf_iterator<char>(),
                         std::istreambuf_iterator<char>(f2.rdbuf()));
    }

    /**
     * @brief Get file size
     * @param filepath File path
     * @return File size in bytes, or -1 on error
     */
    long long GetFileSize(const std::string& filepath) {
        try {
            return std::filesystem::file_size(filepath);
        } catch (...) {
            return -1;
        }
    }

private:
    std::unique_ptr<TftpServer> server_;
};

// ==== BASIC FUNCTIONALITY TESTS ====

TEST_F(CurlTftpTest, SmallTextFileDownload) {
    std::string local_download = std::string(kCurlTestRootDir) + "/downloaded_small_text.txt";
    
    // Remove download file if it exists
    std::filesystem::remove(local_download);
    
    // Download using curl
    ASSERT_TRUE(CurlDownloadFile("small_text.txt", local_download))
        << "Failed to download small text file";
    
    // Verify file exists and has correct content
    ASSERT_TRUE(std::filesystem::exists(local_download))
        << "Downloaded file does not exist";
    
    // Compare with original
    std::string original_file = std::string(kCurlTestRootDir) + "/small_text.txt";
    ASSERT_TRUE(CompareFiles(original_file, local_download))
        << "Downloaded file content does not match original";
    
    LOG_INFO("Small text file download test completed successfully");
}

TEST_F(CurlTftpTest, SmallTextFileUpload) {
    // Create a new file to upload
    std::string upload_content = "This is a test upload via curl TFTP.\nSecond line of upload test.\n";
    std::string local_upload = std::string(kCurlTestRootDir) + "/upload_source.txt";
    WriteTextFile("upload_source.txt", upload_content);
    
    // Upload using curl
    std::string remote_filename = "curl_uploaded_file.txt";
    ASSERT_TRUE(CurlUploadFile(local_upload, remote_filename))
        << "Failed to upload file via curl";
    
    // Verify uploaded file exists on server
    std::string server_file = std::string(kCurlTestRootDir) + "/" + remote_filename;
    
    // Wait for file to be written (handle async operations)
    bool file_appeared = false;
    for (int i = 0; i < 50; ++i) {  // Wait up to 5 seconds
        if (std::filesystem::exists(server_file)) {
            file_appeared = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    ASSERT_TRUE(file_appeared) << "Uploaded file not found on server: " << server_file;
    
    // Compare content
    ASSERT_TRUE(CompareFiles(local_upload, server_file))
        << "Uploaded file content does not match source";
    
    LOG_INFO("Small text file upload test completed successfully");
}

TEST_F(CurlTftpTest, BinaryFileDownload) {
    std::string local_download = std::string(kCurlTestRootDir) + "/downloaded_small_binary.bin";
    
    // Remove download file if it exists
    std::filesystem::remove(local_download);
    
    // Download using curl
    ASSERT_TRUE(CurlDownloadFile("small_binary.bin", local_download))
        << "Failed to download small binary file";
    
    // Verify file exists and has correct content
    ASSERT_TRUE(std::filesystem::exists(local_download))
        << "Downloaded binary file does not exist";
    
    // Compare with original
    std::string original_file = std::string(kCurlTestRootDir) + "/small_binary.bin";
    ASSERT_TRUE(CompareFiles(original_file, local_download))
        << "Downloaded binary file content does not match original";
    
    // Verify file size
    ASSERT_EQ(GetFileSize(original_file), GetFileSize(local_download))
        << "Binary file size mismatch";
    
    LOG_INFO("Binary file download test completed successfully");
}

TEST_F(CurlTftpTest, BinaryFileUpload) {
    // Create a binary file to upload
    std::string local_upload = std::string(kCurlTestRootDir) + "/upload_binary.bin";
    CreateBinaryFile("upload_binary.bin", 1024);  // 1KB binary file
    
    // Upload using curl
    std::string remote_filename = "curl_uploaded_binary.bin";
    ASSERT_TRUE(CurlUploadFile(local_upload, remote_filename))
        << "Failed to upload binary file via curl";
    
    // Verify uploaded file exists on server
    std::string server_file = std::string(kCurlTestRootDir) + "/" + remote_filename;
    
    // Wait for file to be written
    bool file_appeared = false;
    for (int i = 0; i < 50; ++i) {
        if (std::filesystem::exists(server_file)) {
            file_appeared = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    ASSERT_TRUE(file_appeared) << "Uploaded binary file not found on server: " << server_file;
    
    // Compare content
    ASSERT_TRUE(CompareFiles(local_upload, server_file))
        << "Uploaded binary file content does not match source";
    
    LOG_INFO("Binary file upload test completed successfully");
}

// ==== FILE SIZE TESTS ====

TEST_F(CurlTftpTest, MediumSizeFileTransfer) {
    // Test medium text file download
    std::string local_download = std::string(kCurlTestRootDir) + "/downloaded_medium_text.txt";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadFile("medium_text.txt", local_download))
        << "Failed to download medium text file";
    
    std::string original_file = std::string(kCurlTestRootDir) + "/medium_text.txt";
    ASSERT_TRUE(CompareFiles(original_file, local_download))
        << "Medium text file content mismatch";
    
    // Test medium binary file download
    local_download = std::string(kCurlTestRootDir) + "/downloaded_medium_binary.bin";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadFile("medium_binary.bin", local_download))
        << "Failed to download medium binary file";
    
    original_file = std::string(kCurlTestRootDir) + "/medium_binary.bin";
    ASSERT_TRUE(CompareFiles(original_file, local_download))
        << "Medium binary file content mismatch";
    
    LOG_INFO("Medium size file transfer test completed successfully");
}

TEST_F(CurlTftpTest, LargeFileTransfer) {
    // Test large text file download
    std::string local_download = std::string(kCurlTestRootDir) + "/downloaded_large_text.txt";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadFile("large_text.txt", local_download))
        << "Failed to download large text file";
    
    std::string original_file = std::string(kCurlTestRootDir) + "/large_text.txt";
    ASSERT_TRUE(CompareFiles(original_file, local_download))
        << "Large text file content mismatch";
    
    // Test large binary file download (more demanding)
    local_download = std::string(kCurlTestRootDir) + "/downloaded_large_binary.bin";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadFile("large_binary.bin", local_download))
        << "Failed to download large binary file";
    
    original_file = std::string(kCurlTestRootDir) + "/large_binary.bin";
    ASSERT_TRUE(CompareFiles(original_file, local_download))
        << "Large binary file content mismatch";
    
    LOG_INFO("Large file transfer test completed successfully");
}

TEST_F(CurlTftpTest, EmptyFileTransfer) {
    // Test empty file download
    std::string local_download = std::string(kCurlTestRootDir) + "/downloaded_empty.txt";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadFile("empty_file.txt", local_download))
        << "Failed to download empty file";
    
    ASSERT_TRUE(std::filesystem::exists(local_download))
        << "Downloaded empty file does not exist";
    
    ASSERT_EQ(GetFileSize(local_download), 0)
        << "Empty file should have size 0";
    
    // Test empty file upload
    std::string local_empty = std::string(kCurlTestRootDir) + "/upload_empty.txt";
    WriteTextFile("upload_empty.txt", "");
    
    ASSERT_TRUE(CurlUploadFile(local_empty, "curl_uploaded_empty.txt"))
        << "Failed to upload empty file";
    
    LOG_INFO("Empty file transfer test completed successfully");
}

// ==== ERROR CONDITION TESTS ====

TEST_F(CurlTftpTest, FileNotFoundError) {
    // Attempt to download non-existent file
    std::string local_download = std::string(kCurlTestRootDir) + "/nonexistent_download.txt";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadExpectFailure("nonexistent_file_12345.txt"))
        << "Download should have failed for non-existent file";
    
    // Verify no file was created
    ASSERT_FALSE(std::filesystem::exists(local_download))
        << "No file should have been created for failed download";
    
    LOG_INFO("File not found error test completed successfully");
}

TEST_F(CurlTftpTest, InvalidFileNameHandling) {
    // Test various edge cases for filenames
    std::vector<std::string> invalid_files = {
        "../../../etc/passwd",  // Directory traversal attempt
        "..\\..\\..\\windows\\system32\\config\\sam",  // Windows path traversal
        "/etc/shadow",  // Absolute path
        "con.txt",  // Windows reserved name (on Windows)
        "file with spaces but no quotes"  // Spaces without proper quoting
    };
    
    for (const auto& invalid_file : invalid_files) {
        LOG_INFO("Testing invalid filename: %s", invalid_file.c_str());
        EXPECT_TRUE(CurlDownloadExpectFailure(invalid_file))
            << "Download should have failed for invalid filename: " << invalid_file;
    }
    
    LOG_INFO("Invalid filename handling test completed successfully");
}

// ==== TRANSFER MODE TESTS ====

TEST_F(CurlTftpTest, TransferModeComparison) {
    // Create a test file with mixed line endings for mode testing
    std::string mixed_content = "Line 1\r\nLine 2\nLine 3\r\nFinal line\n";
    std::string test_file = std::string(kCurlTestRootDir) + "/mode_test.txt";
    WriteTextFile("mode_test.txt", mixed_content);
    
    // Download in binary mode
    std::string binary_download = std::string(kCurlTestRootDir) + "/mode_test_binary.txt";
    std::filesystem::remove(binary_download);
    ASSERT_TRUE(CurlDownloadFile("mode_test.txt", binary_download, "binary"))
        << "Failed to download in binary mode";
    
    // Download in ASCII mode (if curl supports it for TFTP)
    std::string ascii_download = std::string(kCurlTestRootDir) + "/mode_test_ascii.txt";
    std::filesystem::remove(ascii_download);
    
    // Note: curl TFTP implementation may not distinguish between modes
    // This test verifies that the mode parameter doesn't break functionality
    ASSERT_TRUE(CurlDownloadFile("mode_test.txt", ascii_download, "ascii"))
        << "Failed to download in ASCII mode";
    
    // Both files should exist
    ASSERT_TRUE(std::filesystem::exists(binary_download))
        << "Binary mode download file missing";
    ASSERT_TRUE(std::filesystem::exists(ascii_download))
        << "ASCII mode download file missing";
    
    LOG_INFO("Transfer mode comparison test completed successfully");
}

// ==== CONCURRENT TRANSFER TESTS ====

TEST_F(CurlTftpTest, ConcurrentDownloads) {
    const int num_concurrent = 3;  // Conservative number for stability
    std::vector<std::thread> download_threads;
    std::vector<bool> download_results(num_concurrent, false);
    
    // Launch concurrent downloads
    for (int i = 0; i < num_concurrent; ++i) {
        download_threads.emplace_back([this, i, &download_results]() {
            std::string remote_file;
            std::string local_file;
            
            // Alternate between different file types
            switch (i % 3) {
                case 0:
                    remote_file = "small_text.txt";
                    local_file = std::string(kCurlTestRootDir) + "/concurrent_text_" + std::to_string(i) + ".txt";
                    break;
                case 1:
                    remote_file = "small_binary.bin";
                    local_file = std::string(kCurlTestRootDir) + "/concurrent_binary_" + std::to_string(i) + ".bin";
                    break;
                case 2:
                    remote_file = "medium_text.txt";
                    local_file = std::string(kCurlTestRootDir) + "/concurrent_medium_" + std::to_string(i) + ".txt";
                    break;
            }
            
            std::filesystem::remove(local_file);
            download_results[i] = CurlDownloadFile(remote_file, local_file);
            
            LOG_INFO("Concurrent download %d completed with result: %s", 
                    i, download_results[i] ? "SUCCESS" : "FAILURE");
        });
    }
    
    // Wait for all downloads to complete
    for (auto& thread : download_threads) {
        thread.join();
    }
    
    // Verify all downloads succeeded
    for (int i = 0; i < num_concurrent; ++i) {
        ASSERT_TRUE(download_results[i]) 
            << "Concurrent download " << i << " failed";
    }
    
    LOG_INFO("Concurrent downloads test completed successfully");
}

TEST_F(CurlTftpTest, ConcurrentUploads) {
    const int num_concurrent = 2;  // Conservative for uploads
    std::vector<std::thread> upload_threads;
    std::vector<bool> upload_results(num_concurrent, false);
    
    // Create source files for upload
    for (int i = 0; i < num_concurrent; ++i) {
        std::string content = "Concurrent upload test " + std::to_string(i) + "\n";
        content += "Thread ID: " + std::to_string(i) + "\n";
        content += "Test data for concurrent upload validation.\n";
        WriteTextFile("concurrent_upload_source_" + std::to_string(i) + ".txt", content);
    }
    
    // Launch concurrent uploads
    for (int i = 0; i < num_concurrent; ++i) {
        upload_threads.emplace_back([this, i, &upload_results]() {
            std::string local_file = std::string(kCurlTestRootDir) + "/concurrent_upload_source_" + std::to_string(i) + ".txt";
            std::string remote_file = "concurrent_uploaded_" + std::to_string(i) + ".txt";
            
            upload_results[i] = CurlUploadFile(local_file, remote_file);
            
            LOG_INFO("Concurrent upload %d completed with result: %s", 
                    i, upload_results[i] ? "SUCCESS" : "FAILURE");
        });
    }
    
    // Wait for all uploads to complete
    for (auto& thread : upload_threads) {
        thread.join();
    }
    
    // Verify all uploads succeeded
    for (int i = 0; i < num_concurrent; ++i) {
        ASSERT_TRUE(upload_results[i]) 
            << "Concurrent upload " << i << " failed";
        
        // Verify uploaded file exists
        std::string server_file = std::string(kCurlTestRootDir) + "/concurrent_uploaded_" + std::to_string(i) + ".txt";
        
        // Wait for file to appear
        bool file_found = false;
        for (int retry = 0; retry < 30; ++retry) {
            if (std::filesystem::exists(server_file)) {
                file_found = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        ASSERT_TRUE(file_found) 
            << "Concurrent uploaded file " << i << " not found on server";
    }
    
    LOG_INFO("Concurrent uploads test completed successfully");
}

// ==== PERFORMANCE AND STRESS TESTS ====

TEST_F(CurlTftpTest, TransferPerformanceTest) {
    // Measure transfer time for large file
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string local_download = std::string(kCurlTestRootDir) + "/performance_download.bin";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadFile("large_binary.bin", local_download))
        << "Performance test download failed";
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Get file size for throughput calculation
    long long file_size = GetFileSize(local_download);
    ASSERT_GT(file_size, 0) << "Downloaded file should not be empty";
    
    double throughput_mbps = (static_cast<double>(file_size) / (1024.0 * 1024.0)) / 
                            (static_cast<double>(duration.count()) / 1000.0);
    
    LOG_INFO("Performance test results:");
    LOG_INFO("File size: %lld bytes", file_size);
    LOG_INFO("Transfer time: %lld ms", duration.count());
    LOG_INFO("Throughput: %.2f MB/s", throughput_mbps);
    
    // Basic performance expectation (should complete within reasonable time)
    ASSERT_LT(duration.count(), 30000) << "Transfer took too long (>30 seconds)";
    
    LOG_INFO("Transfer performance test completed successfully");
}

// ==== SPECIAL FILENAME TESTS ====

TEST_F(CurlTftpTest, SpecialFilenameHandling) {
    // Test file with underscores (should work)
    std::string local_download = std::string(kCurlTestRootDir) + "/downloaded_special_filename.txt";
    std::filesystem::remove(local_download);
    
    ASSERT_TRUE(CurlDownloadFile("test_file_with_underscores.txt", local_download))
        << "Failed to download file with underscores in filename";
    
    ASSERT_TRUE(std::filesystem::exists(local_download))
        << "Downloaded file with special filename does not exist";
    
    // Verify content
    std::string original_file = std::string(kCurlTestRootDir) + "/test_file_with_underscores.txt";
    ASSERT_TRUE(CompareFiles(original_file, local_download))
        << "Special filename file content mismatch";
    
    LOG_INFO("Special filename handling test completed successfully");
}

// ==== PROTOCOL REGRESSION TESTS ====

TEST_F(CurlTftpTest, OddSizeBinaryFileTransfer) {
    /**
     * @brief Test for non-multiple-of-512 byte files (regression test)
     * 
     * This test validates the recent fix for TFTP protocol handling of files
     * that are NOT exact multiples of 512 bytes. The bug was related to 
     * incorrect termination packet handling for files with sizes that are 
     * exact multiples of 512 bytes.
     * 
     * Test case: 1026-byte binary file
     * - Block #1: 512 bytes
     * - Block #2: 512 bytes  
     * - Block #3: 2 bytes (final block, naturally terminates transfer)
     * 
     * This ensures that files with odd sizes still work correctly after
     * the protocol fix for multiple-of-512 files.
     */
    
    const size_t kOddBinarySize = 1026;  // 512 + 512 + 2 bytes
    const std::string kOddBinaryFileName = "odd_size_binary.bin";
    
    // Create the 1026-byte binary test file with deterministic pattern
    std::string original_file = std::string(kCurlTestRootDir) + "/" + kOddBinaryFileName;
    std::ofstream file(original_file, std::ios::binary);
    ASSERT_TRUE(file.good()) << "Failed to create 1026-byte test file: " << original_file;
    
    // Create deterministic binary pattern for verification
    std::vector<uint8_t> pattern(kOddBinarySize);
    for (size_t i = 0; i < kOddBinarySize; ++i) {
        // Use a different pattern than other binary files for uniqueness
        pattern[i] = static_cast<uint8_t>((i * 73 + 41) % 256);
    }
    
    file.write(reinterpret_cast<const char*>(pattern.data()), static_cast<std::streamsize>(kOddBinarySize));
    file.close();
    
    // Verify test file was created correctly
    ASSERT_TRUE(std::filesystem::exists(original_file)) 
        << "1026-byte test file was not created";
    ASSERT_EQ(GetFileSize(original_file), kOddBinarySize) 
        << "Test file size is incorrect";
    
    LOG_INFO("Created 1026-byte binary test file: %s", kOddBinaryFileName.c_str());
    
    // ==== TEST UPLOAD FUNCTIONALITY ====
    
    // Upload the 1026-byte file using curl TFTP
    std::string uploaded_filename = "uploaded_odd_size_binary.bin";
    ASSERT_TRUE(CurlUploadFile(original_file, uploaded_filename))
        << "Failed to upload 1026-byte binary file via curl TFTP";
    
    // Verify uploaded file exists on server
    std::string server_uploaded_file = std::string(kCurlTestRootDir) + "/" + uploaded_filename;
    
    // Wait for file to be written (handle async operations)
    bool upload_appeared = false;
    for (int i = 0; i < 50; ++i) {  // Wait up to 5 seconds
        if (std::filesystem::exists(server_uploaded_file)) {
            upload_appeared = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    ASSERT_TRUE(upload_appeared) 
        << "Uploaded 1026-byte file not found on server: " << server_uploaded_file;
    
    // Verify uploaded file size
    ASSERT_EQ(GetFileSize(server_uploaded_file), kOddBinarySize)
        << "Uploaded file size does not match original (1026 bytes)";
    
    // Verify uploaded file content integrity
    ASSERT_TRUE(CompareFiles(original_file, server_uploaded_file))
        << "Uploaded 1026-byte file content does not match original";
    
    LOG_INFO("1026-byte binary file upload completed successfully");
    
    // ==== TEST DOWNLOAD FUNCTIONALITY ====
    
    // Download the same 1026-byte file
    std::string downloaded_file = std::string(kCurlTestRootDir) + "/downloaded_odd_size_binary.bin";
    std::filesystem::remove(downloaded_file);  // Ensure clean state
    
    ASSERT_TRUE(CurlDownloadFile(kOddBinaryFileName, downloaded_file))
        << "Failed to download 1026-byte binary file via curl TFTP";
    
    // Verify downloaded file exists
    ASSERT_TRUE(std::filesystem::exists(downloaded_file))
        << "Downloaded 1026-byte file does not exist";
    
    // Verify downloaded file size
    ASSERT_EQ(GetFileSize(downloaded_file), kOddBinarySize)
        << "Downloaded file size does not match original (1026 bytes)";
    
    // Verify downloaded file content integrity
    ASSERT_TRUE(CompareFiles(original_file, downloaded_file))
        << "Downloaded 1026-byte file content does not match original";
    
    LOG_INFO("1026-byte binary file download completed successfully");
    
    // ==== ADDITIONAL VERIFICATION ====
    
    // Cross-verify: uploaded file should match downloaded file
    ASSERT_TRUE(CompareFiles(server_uploaded_file, downloaded_file))
        << "Uploaded and downloaded files should be identical";
    
    // Verify exact byte pattern integrity at critical boundaries
    std::ifstream verify_file(downloaded_file, std::ios::binary);
    ASSERT_TRUE(verify_file.good()) << "Cannot open downloaded file for verification";
    
    std::vector<uint8_t> downloaded_data(kOddBinarySize);
    verify_file.read(reinterpret_cast<char*>(downloaded_data.data()), kOddBinarySize);
    verify_file.close();
    
    // Verify first block (bytes 0-511)
    bool first_block_ok = std::equal(pattern.begin(), pattern.begin() + 512, downloaded_data.begin());
    ASSERT_TRUE(first_block_ok) << "First 512-byte block corrupted";
    
    // Verify second block (bytes 512-1023)
    bool second_block_ok = std::equal(pattern.begin() + 512, pattern.begin() + 1024, downloaded_data.begin() + 512);
    ASSERT_TRUE(second_block_ok) << "Second 512-byte block corrupted";
    
    // Verify final 2 bytes (bytes 1024-1025)
    bool final_bytes_ok = std::equal(pattern.begin() + 1024, pattern.end(), downloaded_data.begin() + 1024);
    ASSERT_TRUE(final_bytes_ok) << "Final 2 bytes corrupted";
    
    LOG_INFO("Odd size binary file transfer test completed successfully");
    LOG_INFO("Verified 1026-byte file with pattern: Block1(512) + Block2(512) + Block3(2)");
}

// Note: main() function is provided by the main test file (tftp_server_test.cpp)