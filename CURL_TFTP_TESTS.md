# TFTP Server Curl Integration Tests

This document describes the comprehensive curl-based TFTP client tests for the TFTPServer implementation.

## Overview

The curl integration tests validate the TFTP server implementation using curl's TFTP client functionality. These tests cover functionality, performance, and security aspects of the TFTP server.

## Test Suites

### 1. Integration Tests (`CurlTftpTest`)
**File**: `tests/tftp_curl_integration_test.cpp`
**Test Class**: `CurlTftpTest`
**Purpose**: Basic functionality validation

#### Test Cases:
- **SmallTextFileDownload**: Download small text files
- **SmallTextFileUpload**: Upload small text files
- **BinaryFileDownload**: Download binary files
- **BinaryFileUpload**: Upload binary files
- **MediumSizeFileTransfer**: Transfer medium-sized files (4KB)
- **LargeFileTransfer**: Transfer large files (1MB)
- **EmptyFileTransfer**: Handle empty files
- **FileNotFoundError**: Error handling for non-existent files
- **InvalidFileNameHandling**: Security testing for invalid filenames
- **TransferModeComparison**: Test different transfer modes
- **ConcurrentDownloads**: Multiple simultaneous downloads
- **ConcurrentUploads**: Multiple simultaneous uploads
- **TransferPerformanceTest**: Basic performance measurement
- **SpecialFilenameHandling**: Files with underscores and special characters

### 2. Performance Tests (`CurlTftpPerformanceTest`)
**File**: `tests/tftp_curl_performance_test.cpp`
**Test Class**: `CurlTftpPerformanceTest`
**Purpose**: Performance benchmarking and analysis

#### Test Cases:
- **DownloadPerformanceBySize**: Benchmark downloads (1KB to 5MB)
- **UploadPerformanceBySize**: Benchmark uploads (1KB to 1MB)
- **BinaryVsTextPerformanceComparison**: Compare binary vs text transfer speeds
- **RepeatedTransferConsistency**: Measure performance consistency
- **TimeoutHandling**: Test timeout behavior

#### Performance Metrics:
- Transfer throughput (MB/s)
- Transfer duration (milliseconds)
- File size validation
- Consistency analysis (coefficient of variation)

### 3. Security Tests (`CurlTftpSecurityTest`)
**File**: `tests/tftp_curl_security_test.cpp`
**Test Class**: `CurlTftpSecurityTest`
**Purpose**: Security validation and edge case handling

#### Test Cases:
- **DirectoryTraversalPrevention**: Block path traversal attacks
- **DirectoryTraversalUpload**: Block malicious uploads
- **ValidFilenameHandling**: Allow legitimate filenames
- **InvalidFilenameRejection**: Reject dangerous filenames
- **SpecialCharacterHandling**: Handle special characters safely
- **ProtocolAbuseDetection**: Detect and handle protocol abuse
- **ResourceExhaustionPrevention**: Handle rapid requests
- **ConcurrentConnectionLimits**: Manage concurrent connections

## Prerequisites

### System Requirements:
- **curl**: Version 7.x or higher with TFTP support
- **CMake**: Version 3.20 or higher
- **GoogleTest**: Installed via vcpkg
- **C++17** compatible compiler

### Verify curl TFTP Support:
```bash
curl --version
# Should show "tftp" in the Protocols list
```

### Test if curl can access TFTP:
```bash
curl --help | grep tftp
# Should show TFTP-related options
```

## Building and Running Tests

### Build the Tests:
```bash
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg.cmake
make tftpserver_tests
```

### Run All Tests:
```bash
ctest
```

### Run Specific Test Suites:

#### Integration Tests Only:
```bash
ctest -R CurlTftpIntegrationTests -V
```

#### Performance Tests Only:
```bash
ctest -R CurlTftpPerformanceTests -V
```

#### Security Tests Only:
```bash
ctest -R CurlTftpSecurityTests -V
```

#### Run Tests by Label:
```bash
# Run all curl-related tests
ctest -L curl -V

# Run performance tests
ctest -L performance -V

# Run security tests
ctest -L security -V
```

### Run Tests Directly:
```bash
./bin/tftpserver_tests --gtest_filter="CurlTftpTest.*"
./bin/tftpserver_tests --gtest_filter="CurlTftpPerformanceTest.*"
./bin/tftpserver_tests --gtest_filter="CurlTftpSecurityTest.*"
```

## Test Configuration

### Port Configuration:
- Integration Tests: Port 6970
- Performance Tests: Port 6971
- Security Tests: Port 6972

### Timeout Settings:
- Integration Tests: 600 seconds (10 minutes)
- Performance Tests: 900 seconds (15 minutes)
- Security Tests: 300 seconds (5 minutes)

### Test Files:
Tests create temporary directories and files:
- `./curl_test_files/` (Integration tests)
- `./curl_perf_test_files/` (Performance tests)
- `./curl_security_test_files/` (Security tests)

## Expected Test Results

### Performance Benchmarks:
- **Small files (1KB)**: Should complete in < 1 second
- **Medium files (100KB)**: Should achieve > 0.1 MB/s
- **Large files (1MB+)**: Should achieve > 0.1 MB/s
- **Consistency**: Coefficient of variation < 30%

### Security Expectations:
- **Directory traversal**: All attempts should be blocked
- **Invalid filenames**: Should be rejected safely
- **Concurrent connections**: Should handle 5+ simultaneous connections
- **Rapid requests**: Should handle 10 requests in reasonable time

## Troubleshooting

### Common Issues:

#### 1. "curl not found":
```bash
# Windows (using chocolatey)
choco install curl

# Ubuntu/Debian
sudo apt-get install curl

# CentOS/RHEL
sudo yum install curl
```

#### 2. "TFTP not supported":
Ensure curl was compiled with TFTP support. Some minimal curl installations might not include TFTP.

#### 3. "Connection refused":
- Check if TFTP server is running
- Verify port numbers (6970-6972) are not blocked by firewall
- Ensure no other services are using the test ports

#### 4. "Permission denied":
- Ensure test directories are writable
- On Unix/Linux, check file permissions
- Run with appropriate user privileges

#### 5. "Timeout errors":
- Network latency might cause timeouts
- Adjust timeout values in test code if needed
- Check system resource availability

### Debug Options:

#### Verbose curl output:
Modify test code to add `-v` flag to curl commands for detailed protocol information.

#### Enable debug logging:
Set environment variables:
```bash
export GTEST_COLOR=1
export GTEST_BRIEF=0
ctest -V
```

#### Run single test:
```bash
./bin/tftpserver_tests --gtest_filter="CurlTftpTest.SmallTextFileDownload"
```

## Test Coverage

The curl-based tests provide coverage for:

### Functional Coverage:
- ✅ File upload (PUT/WRQ)
- ✅ File download (GET/RRQ)
- ✅ Binary and text transfers
- ✅ Various file sizes (empty to 5MB)
- ✅ Error conditions
- ✅ Concurrent operations

### Protocol Coverage:
- ✅ TFTP opcodes (RRQ, WRQ, DATA, ACK, ERROR)
- ✅ Block number handling
- ✅ Transfer modes (netascii, octet)
- ✅ Timeout and retry behavior
- ✅ End-of-transfer detection

### Security Coverage:
- ✅ Directory traversal prevention
- ✅ Filename validation
- ✅ Special character handling
- ✅ Resource exhaustion protection
- ✅ Concurrent connection limits

## Integration with CI/CD

### GitHub Actions Example:
```yaml
- name: Install curl
  run: |
    sudo apt-get update
    sudo apt-get install -y curl
    
- name: Run TFTP curl tests
  run: |
    cd build
    ctest -L curl --output-on-failure
```

### Test Result Analysis:
The tests generate XML output in `${CMAKE_BINARY_DIR}/test_results/` for integration with test reporting tools.

## Contributing

### Adding New Tests:
1. Add test cases to appropriate test class
2. Follow naming convention: `TestCategory[Specific]Description`
3. Use descriptive log messages with `LOG_INFO`/`PERF_LOG`/`SEC_LOG`
4. Clean up test files in teardown
5. Update this documentation

### Performance Test Guidelines:
- Use deterministic test data for reproducible results
- Measure both throughput and latency
- Include statistical analysis for consistency
- Set reasonable performance expectations
- Document system requirements for benchmarks

### Security Test Guidelines:
- Test both positive and negative cases
- Cover common attack vectors
- Validate error handling
- Test edge cases and boundary conditions
- Document expected security behaviors