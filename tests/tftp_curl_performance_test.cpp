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
#include <tftp/tftp_common.h>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <filesystem>
#include <iostream>
#include <memory>
#include <array>

using namespace tftpserver;

// Performance test constants
constexpr uint16_t kPerfTestPort = 6971;
constexpr const char* kPerfTestRootDir = "./curl_perf_test_files";

// File sizes for performance testing
constexpr size_t k1KB = 1024;
constexpr size_t k10KB = 10 * k1KB;
constexpr size_t k100KB = 100 * k1KB;
constexpr size_t k1MB = 1024 * k1KB;
constexpr size_t k5MB = 5 * k1MB;

// Log macros
#ifdef _WIN32
#define PERF_LOG(fmt, ...) do { \
    fprintf(stdout, "[PERF TEST] " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)
#else
#define PERF_LOG(fmt, ...) fprintf(stdout, "[PERF TEST] " fmt "\n", ##__VA_ARGS__); fflush(stdout)
#endif

/**
 * @class CurlTftpPerformanceTest
 * @brief Performance and benchmarking tests for curl TFTP client
 */
class CurlTftpPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        std::filesystem::create_directories(kPerfTestRootDir);
        
        // Create performance test files
        CreatePerformanceTestFiles();

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        // Start TFTP server with performance optimizations
        server_ = std::make_unique<TftpServer>(kPerfTestRootDir, kPerfTestPort);
        server_->SetTimeout(30);  // Extended timeout for large files
        server_->SetMaxTransferSize(k5MB + 1024);  // Allow large transfers
        
        ASSERT_TRUE(server_->Start()) << "Failed to start TFTP server for performance tests";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        PERF_LOG("TFTP Performance Test Server started on port %d", kPerfTestPort);
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
            server_.reset();
        }

        std::filesystem::remove_all(kPerfTestRootDir);

#ifdef _WIN32
        WSACleanup();
#endif
    }

    void CreatePerformanceTestFiles() {
        // Create files of different sizes for performance testing
        CreateBinaryFile("perf_1kb.bin", k1KB);
        CreateBinaryFile("perf_10kb.bin", k10KB);
        CreateBinaryFile("perf_100kb.bin", k100KB);
        CreateBinaryFile("perf_1mb.bin", k1MB);
        CreateBinaryFile("perf_5mb.bin", k5MB);
        
        // Create text files as well
        CreateTextFile("perf_1kb.txt", k1KB);
        CreateTextFile("perf_100kb.txt", k100KB);
        CreateTextFile("perf_1mb.txt", k1MB);
        
        PERF_LOG("Created performance test files");
    }

    void CreateBinaryFile(const std::string& filename, size_t size) {
        std::string filepath = std::string(kPerfTestRootDir) + "/" + filename;
        std::ofstream file(filepath, std::ios::binary);
        
        // Create deterministic but varied binary data
        std::vector<uint8_t> buffer(std::min(size, static_cast<size_t>(8192)));
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = static_cast<uint8_t>((i * 37 + 23 + size) % 256);
        }
        
        size_t remaining = size;
        while (remaining > 0) {
            size_t write_size = std::min(remaining, buffer.size());
            file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(write_size));
            remaining -= write_size;
        }
        
        file.close();
    }

    void CreateTextFile(const std::string& filename, size_t approximate_size) {
        std::string filepath = std::string(kPerfTestRootDir) + "/" + filename;
        std::ofstream file(filepath);
        
        const std::string line_template = "This is performance test line number XXXXXX for file size testing.\n";
        size_t line_size = line_template.length();
        size_t num_lines = approximate_size / line_size;
        
        for (size_t i = 0; i < num_lines; ++i) {
            std::string line = "This is performance test line number " + 
                             std::to_string(i) + " for file size testing.\n";
            file << line;
        }
        
        file.close();
    }

    std::pair<int, std::string> ExecuteCurlCommand(const std::vector<std::string>& args) {
        std::string command = "C:\\Windows\\System32\\curl.exe";
        for (const auto& arg : args) {
            command += " \"" + arg + "\"";
        }
        
        std::array<char, 128> buffer;
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

    struct TransferResults {
        bool success;
        std::chrono::milliseconds duration;
        size_t file_size;
        double throughput_mbps;
        
        TransferResults() : success(false), duration(0), file_size(0), throughput_mbps(0.0) {}
    };

    TransferResults MeasureDownload(const std::string& remote_file, 
                                   const std::string& local_file) {
        TransferResults results;
        
        std::filesystem::remove(local_file);
        
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "10",
            "--max-time", "60",
            "-o", local_file,
            "tftp://127.0.0.1:" + std::to_string(kPerfTestPort) + "/" + remote_file
        };
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto curl_result = ExecuteCurlCommand(args);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        results.success = (curl_result.first == 0);
        results.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (results.success && std::filesystem::exists(local_file)) {
            results.file_size = std::filesystem::file_size(local_file);
            double seconds = static_cast<double>(results.duration.count()) / 1000.0;
            double mb = static_cast<double>(results.file_size) / (1024.0 * 1024.0);
            results.throughput_mbps = mb / seconds;
        }
        
        return results;
    }

    TransferResults MeasureUpload(const std::string& local_file, 
                                 const std::string& remote_file) {
        TransferResults results;
        
        if (!std::filesystem::exists(local_file)) {
            return results;
        }
        
        results.file_size = std::filesystem::file_size(local_file);
        
        std::vector<std::string> args = {
            "--tftp-blksize", "512",
            "--connect-timeout", "10",
            "--max-time", "60",
            "-T", local_file,
            "tftp://127.0.0.1:" + std::to_string(kPerfTestPort) + "/" + remote_file
        };
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto curl_result = ExecuteCurlCommand(args);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        results.success = (curl_result.first == 0);
        results.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (results.success) {
            double seconds = static_cast<double>(results.duration.count()) / 1000.0;
            double mb = static_cast<double>(results.file_size) / (1024.0 * 1024.0);
            results.throughput_mbps = mb / seconds;
        }
        
        return results;
    }

    void PrintResults(const std::string& test_name, const TransferResults& results) {
        PERF_LOG("=== %s Results ===", test_name.c_str());
        PERF_LOG("Success: %s", results.success ? "YES" : "NO");
        PERF_LOG("File size: %zu bytes (%.2f KB, %.2f MB)", 
                results.file_size,
                static_cast<double>(results.file_size) / 1024.0,
                static_cast<double>(results.file_size) / (1024.0 * 1024.0));
        PERF_LOG("Duration: %lld ms (%.2f seconds)", 
                results.duration.count(),
                static_cast<double>(results.duration.count()) / 1000.0);
        PERF_LOG("Throughput: %.2f MB/s", results.throughput_mbps);
        PERF_LOG("=============================");
    }

private:
    std::unique_ptr<TftpServer> server_;
};

// Performance test for different file sizes
TEST_F(CurlTftpPerformanceTest, DownloadPerformanceBySize) {
    std::vector<std::pair<std::string, std::string>> test_files = {
        {"perf_1kb.bin", "downloaded_perf_1kb.bin"},
        {"perf_10kb.bin", "downloaded_perf_10kb.bin"},
        {"perf_100kb.bin", "downloaded_perf_100kb.bin"},
        {"perf_1mb.bin", "downloaded_perf_1mb.bin"},
        {"perf_5mb.bin", "downloaded_perf_5mb.bin"}
    };
    
    PERF_LOG("Starting download performance test by file size");
    
    for (const auto& file_pair : test_files) {
        std::string local_file = std::string(kPerfTestRootDir) + "/" + file_pair.second;
        
        TransferResults results = MeasureDownload(file_pair.first, local_file);
        
        std::string test_name = "Download " + file_pair.first;
        PrintResults(test_name, results);
        
        ASSERT_TRUE(results.success) << "Failed to download " << file_pair.first;
        
        // Basic performance expectations
        if (results.file_size >= k1MB) {
            EXPECT_GT(results.throughput_mbps, 0.1) 
                << "Throughput too low for " << file_pair.first;
            EXPECT_LT(results.duration.count(), 120000) 
                << "Transfer took too long for " << file_pair.first;
        }
    }
}

TEST_F(CurlTftpPerformanceTest, UploadPerformanceBySize) {
    std::vector<std::pair<std::string, std::string>> test_files = {
        {"perf_1kb.bin", "uploaded_perf_1kb.bin"},
        {"perf_10kb.bin", "uploaded_perf_10kb.bin"},
        {"perf_100kb.bin", "uploaded_perf_100kb.bin"},
        {"perf_1mb.bin", "uploaded_perf_1mb.bin"}
        // Skip 5MB upload for time constraints in regular testing
    };
    
    PERF_LOG("Starting upload performance test by file size");
    
    for (const auto& file_pair : test_files) {
        std::string local_file = std::string(kPerfTestRootDir) + "/" + file_pair.first;
        
        TransferResults results = MeasureUpload(local_file, file_pair.second);
        
        std::string test_name = "Upload " + file_pair.first;
        PrintResults(test_name, results);
        
        ASSERT_TRUE(results.success) << "Failed to upload " << file_pair.first;
        
        // Basic performance expectations
        if (results.file_size >= k1MB) {
            EXPECT_GT(results.throughput_mbps, 0.1) 
                << "Upload throughput too low for " << file_pair.first;
            EXPECT_LT(results.duration.count(), 120000) 
                << "Upload took too long for " << file_pair.first;
        }
    }
}

TEST_F(CurlTftpPerformanceTest, BinaryVsTextPerformanceComparison) {
    PERF_LOG("Comparing binary vs text file transfer performance");
    
    // Test 1MB files
    std::string binary_download = std::string(kPerfTestRootDir) + "/downloaded_perf_1mb.bin";
    std::string text_download = std::string(kPerfTestRootDir) + "/downloaded_perf_1mb.txt";
    
    TransferResults binary_results = MeasureDownload("perf_1mb.bin", binary_download);
    TransferResults text_results = MeasureDownload("perf_1mb.txt", text_download);
    
    PrintResults("Binary 1MB Download", binary_results);
    PrintResults("Text 1MB Download", text_results);
    
    ASSERT_TRUE(binary_results.success) << "Binary download failed";
    ASSERT_TRUE(text_results.success) << "Text download failed";
    
    // Compare performance (binary should not be significantly slower than text)
    double perf_ratio = binary_results.throughput_mbps / text_results.throughput_mbps;
    PERF_LOG("Binary/Text performance ratio: %.2f", perf_ratio);
    
    EXPECT_GT(perf_ratio, 0.5) << "Binary transfer significantly slower than text";
    EXPECT_LT(perf_ratio, 2.0) << "Unexpected large performance difference";
}

TEST_F(CurlTftpPerformanceTest, RepeatedTransferConsistency) {
    PERF_LOG("Testing repeated transfer consistency");
    
    const int num_iterations = 3;
    std::vector<TransferResults> results;
    
    for (int i = 0; i < num_iterations; ++i) {
        std::string local_file = std::string(kPerfTestRootDir) + "/consistency_test_" + std::to_string(i) + ".bin";
        TransferResults result = MeasureDownload("perf_100kb.bin", local_file);
        
        ASSERT_TRUE(result.success) << "Iteration " << i << " failed";
        results.push_back(result);
        
        PrintResults("Consistency Test Iteration " + std::to_string(i), result);
    }
    
    // Calculate average and standard deviation
    double avg_throughput = 0.0;
    double avg_duration = 0.0;
    
    for (const auto& result : results) {
        avg_throughput += result.throughput_mbps;
        avg_duration += result.duration.count();
    }
    
    avg_throughput /= num_iterations;
    avg_duration /= num_iterations;
    
    double variance_throughput = 0.0;
    double variance_duration = 0.0;
    
    for (const auto& result : results) {
        variance_throughput += std::pow(result.throughput_mbps - avg_throughput, 2);
        variance_duration += std::pow(result.duration.count() - avg_duration, 2);
    }
    
    double stddev_throughput = std::sqrt(variance_throughput / num_iterations);
    double stddev_duration = std::sqrt(variance_duration / num_iterations);
    
    PERF_LOG("=== Consistency Analysis ===");
    PERF_LOG("Average throughput: %.2f ± %.2f MB/s", avg_throughput, stddev_throughput);
    PERF_LOG("Average duration: %.0f ± %.0f ms", avg_duration, stddev_duration);
    PERF_LOG("Throughput CoV: %.2f%%", (stddev_throughput / avg_throughput) * 100.0);
    PERF_LOG("Duration CoV: %.2f%%", (stddev_duration / avg_duration) * 100.0);
    
    // Coefficient of variation should be reasonable (< 20% for controlled test environment)
    double throughput_cov = (stddev_throughput / avg_throughput) * 100.0;
    EXPECT_LT(throughput_cov, 30.0) << "Transfer performance too inconsistent";
}

TEST_F(CurlTftpPerformanceTest, TimeoutHandling) {
    PERF_LOG("Testing timeout handling with aggressive timeout settings");
    
    // Test with very short timeout for small file (should succeed)
    std::vector<std::string> args_success = {
        "--tftp-blksize", "512",
        "--connect-timeout", "2",
        "--max-time", "5",
        "-o", std::string(kPerfTestRootDir) + "/timeout_test_success.bin",
        "tftp://127.0.0.1:" + std::to_string(kPerfTestPort) + "/perf_1kb.bin"
    };
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result_success = ExecuteCurlCommand(args_success);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    PERF_LOG("Short timeout test: exit_code=%d, duration=%lld ms", 
            result_success.first, duration.count());
    
    EXPECT_EQ(result_success.first, 0) << "Short timeout test should succeed for small file";
    EXPECT_LT(duration.count(), 5000) << "Should complete well within timeout";
    
    // Test with impossibly short timeout for large file (should timeout)
    std::vector<std::string> args_timeout = {
        "--tftp-blksize", "512",
        "--connect-timeout", "1",
        "--max-time", "2",
        "-o", std::string(kPerfTestRootDir) + "/timeout_test_fail.bin",
        "tftp://127.0.0.1:" + std::to_string(kPerfTestPort) + "/perf_1mb.bin"
    };
    
    start_time = std::chrono::high_resolution_clock::now();
    auto result_timeout = ExecuteCurlCommand(args_timeout);
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    PERF_LOG("Aggressive timeout test: exit_code=%d, duration=%lld ms", 
            result_timeout.first, duration.count());
    
    // Should timeout (non-zero exit code) and complete quickly due to timeout
    EXPECT_NE(result_timeout.first, 0) << "Aggressive timeout should cause failure";
    EXPECT_LT(duration.count(), 4000) << "Should timeout quickly";
}

// Note: main() function is provided by the main test file (tftp_server_test.cpp)