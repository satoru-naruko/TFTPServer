# TFTP Server Library

## Overview

TFTP Server Library is a TFTP server library implemented in C++17.

## Key Features

- **C++17 Compliant**: Supports the latest C++ standard
- **Thread Safe**: Handles concurrent access
- **Comprehensive Testing**: Over 80% test coverage
- **Easy Build**: Automated with vcpkg and CMake

## Quick Start

```cmd
# 1. Navigate to project directory
cd C:\work\repos\TFTPServer

# 2. Setup development environment
setup_dev_env.cmd

# 3. Install dependencies
vcpkg install

# 4. Build the project
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --config Debug

# 5. Run tests
ctest -C Debug --output-on-failure
```

## Testing

The TFTP Server Library includes comprehensive testing with multiple test suites to ensure robust functionality, performance, and security.

### Test Suites Overview

#### 1. Unit Tests (`TftpServerTest`)
- **File**: `tests/tftp_server_test.cpp`
- **Purpose**: Core server functionality testing
- **Tests**: File upload/download, large file transfers, async operations, error handling

#### 2. Curl Integration Tests (`CurlTftpTest`)
- **File**: `tests/tftp_curl_integration_test.cpp`
- **Purpose**: Real-world TFTP client testing using curl
- **Tests**: 14 comprehensive integration tests covering:
  - Small/medium/large file transfers
  - Binary and text file handling
  - Empty file transfers
  - Error conditions (file not found, invalid filenames)
  - Concurrent operations
  - Transfer mode validation

#### 3. Performance Tests (`CurlTftpPerformanceTest`)
- **File**: `tests/tftp_curl_performance_test.cpp`
- **Purpose**: Performance benchmarking and optimization validation
- **Tests**: 5 performance-focused tests including:
  - Throughput measurement by file size (1KB to 5MB)
  - Binary vs text performance comparison
  - Transfer consistency analysis
  - Timeout handling validation

#### 4. Security Tests (`CurlTftpSecurityTest`)
- **File**: `tests/tftp_curl_security_test.cpp` 
- **Purpose**: Security validation and attack prevention
- **Tests**: 8 security-focused tests covering:
  - Directory traversal prevention
  - Filename validation and sanitization
  - Protocol abuse detection
  - Resource exhaustion protection
  - Concurrent connection limits

### Running Tests

#### Prerequisites
- **curl**: Version 7.x+ with TFTP support
- **GoogleTest**: Installed via vcpkg
- **CMake**: 3.20 or higher

#### Verify curl TFTP Support
```cmd
curl --version
# Should show "tftp" in the Protocols list
```

#### Run All Tests
```cmd
cd build
ctest -C Debug --output-on-failure
```

#### Run Specific Test Suites
```cmd
# Integration tests only
ctest -R CurlTftpIntegrationTests -V

# Performance tests only
ctest -R CurlTftpPerformanceTests -V

# Security tests only
ctest -R CurlTftpSecurityTests -V

# All curl-based tests
ctest -R "CurlTftp.*" -V
```

#### Run Individual Tests
```cmd
# Run single test
./DEBUG/bin/tftpserver_tests.exe --gtest_filter="CurlTftpTest.SmallTextFileDownload"

# List all available tests
./DEBUG/bin/tftpserver_tests.exe --gtest_list_tests
```

### Test Coverage

The comprehensive test suite provides:
- **37 total tests** across 4 test suites
- **80%+ code coverage** minimum requirement
- **Functional testing**: Upload/download operations, error handling
- **Performance testing**: Throughput measurement, consistency analysis
- **Security testing**: Attack prevention, input validation
- **Integration testing**: Real-world curl client scenarios

### Test Configuration

- **Port Range**: Tests use ports 6970-6972 (avoiding conflicts)
- **Timeouts**: Extended timeouts for large file transfers (up to 15 minutes)
- **Test Data**: Deterministic test patterns for reproducible results
- **Cleanup**: Automatic cleanup of temporary test files

For detailed testing documentation, see [CURL_TFTP_TESTS.md](CURL_TFTP_TESTS.md).

## Build Instructions

### Prerequisites

- **CMake 3.20 or higher**
- **Visual Studio 2022** (C++17 compatible compiler)
- **vcpkg** (package manager)

### Initial Setup

#### 1. Prepare Development Environment

```cmd
# Navigate to project directory
cd C:\work\repos\TFTPServer

# Setup development environment (recommended to run every time)
setup_dev_env.cmd
```

#### 2. Install Dependencies

```cmd
# Install dependencies defined in vcpkg.json
vcpkg install
```

#### 3. Build the Project

```cmd
# Create build directory (first time only)
mkdir build
cd build

# Configure CMake (specify vcpkg toolchain)
cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..

# Build the project
cmake --build . --config Debug
```

### Clean Build (if needed)

```cmd
rmdir /s /q build && mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --config Debug
```

### Useful Command Aliases

After running `setup_dev_env.cmd`, the following aliases are available:

```cmd
cmake_configure    # Configure CMake
cmake_build       # Execute build
cmake_test        # Run tests
```

### Running the Server

1. Open Command Prompt with Administrator privileges (when using port 69)
2. Start the server with the following command:

```cmd
your_server_app.exe [root_directory] [port_number]
```

Parameters:
- `root_directory`: Directory to save files (defaults to current directory if omitted)
- `port_number`: Port number to use (defaults to 69 if omitted)

Examples:
```cmd
# Start with default settings (port 69)
your_server_app.exe C:\tftp_files

# Start with custom port
your_server_app.exe C:\tftp_files 6969
```

### Important Notes

1. **Administrator Privileges**
   - Administrator privileges are required when using port 69
   - For normal user privileges, specify a port number 1024 or higher

2. **Firewall Settings**
   - Open the used port in Windows Firewall
   - Control Panel → Windows Defender Firewall → Advanced Settings → Inbound Rules

3. **Security**
   - TFTP has no authentication, so use only on trusted networks
   - Enable secure mode whenever possible

## About TFTP Protocol

TFTP stands for "Trivial File Transfer Protocol" and is a simple file transfer protocol. Unlike FTP, it has no authentication and uses UDP to transfer files.

### Protocol Specification

- **RFC**: RFC 1350 (basic specification), RFC 2347-2349 (extensions)
- **Port**: 69 (default)
- **Protocol**: UDP
- **Data Block Size**: 512 bytes
- **Maximum File Size**: Approximately 32MB (typical implementation)

### Packet Format

#### 1. Read Request (RRQ) / Write Request (WRQ)

``` text
   2 bytes    string   1 byte   string   1 byte
  +--------+----------+--------+----------+--------+
  | OpCode | Filename |   0    |  Mode    |   0    |
  +--------+----------+--------+----------+--------+
```

#### 2. Data Packet

``` text
   2 bytes    2 bytes     n bytes
  +--------+-----------+----------+
  | OpCode | Block No. |   Data   |
  +--------+-----------+----------+
```

#### 3. Acknowledgment (ACK)

``` text
   2 bytes    2 bytes
  +--------+-----------+
  | OpCode | Block No. |
  +--------+-----------+
```

#### 4. Error Packet

``` text
   2 bytes    2 bytes       string    1 byte
  +--------+-----------+-------------+--------+
  | OpCode | ErrorCode | Error Msg  |   0    |
  +--------+-----------+-------------+--------+
```

### Operation Codes

| Code | Name | Description |
|------|------|-------------|
| 1 | RRQ | Read Request (file read request) |
| 2 | WRQ | Write Request (file write request) |
| 3 | DATA | Data Packet (data packet) |
| 4 | ACK | Acknowledgment (acknowledgment) |
| 5 | ERROR | Error Packet (error packet) |

### Error Codes

| Code | Description |
|------|-------------|
| 0 | Not defined |
| 1 | File not found |
| 2 | Access violation |
| 3 | Disk full |
| 4 | Illegal operation |
| 5 | Unknown transfer ID |
| 6 | File already exists |
| 7 | No such user |

### Transfer Modes

- **netascii**: For text files (with line ending conversion)
- **octet**: For binary files (no conversion)
- **mail**: For mail transfer (deprecated)

### Use Cases

- Firmware updates for embedded devices
- When simple file transfer is needed
- Network boot (PXE)
- Configuration file distribution
- Log file collection

## System Requirements

### Required

- **C++ Compiler**: Supports C++17 or higher
  - MSVC 2022+
- **CMake**: 3.20 or higher
- **OS**: Windows 11

## Usage

### Basic Usage Example

```cpp
#include <tftp/tftp_server.h>
#include <iostream>

int main() {
    // Create TFTP server
    tftpserver::TftpServer server("./files", 69);
    
    // Enable secure mode
    server.SetSecureMode(true);
    
    // Set timeout
    server.SetTimeout(10);
    
    // Start server
    if (!server.Start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "TFTP server started" << std::endl;
    
    // Some processing...
    
    // Stop server
    server.Stop();
    return 0;
}
```

### Using Custom Callbacks

```cpp
#include <tftp/tftp_server.h>
#include <fstream>
#include <iostream>

int main() {
    tftpserver::TftpServer server("./files", 69);
    
    // Custom read handler
    server.SetReadCallback([](const std::string& filename, std::vector<uint8_t>& data) {
        std::cout << "File read request: " << filename << std::endl;
        
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;
        
        file.seekg(0, std::ios::end);
        data.resize(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(data.data()), data.size());
        
        return true;
    });
    
    // Custom write handler
    server.SetWriteCallback([](const std::string& filename, const std::vector<uint8_t>& data) {
        std::cout << "File write request: " << filename << std::endl;
        
        std::ofstream file(filename, std::ios::binary);
        if (!file) return false;
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        return file.good();
    });
    
    server.Start();
    // ...
}
```

## API Reference

### TftpServer Class

#### Constructor

```cpp
TftpServer(const std::string& root_dir, uint16_t port = 69)
```

- `root_dir`: Root directory for file serving
- `port`: Port number to use (default: 69)

#### Main Methods

```cpp
bool Start()                    // Start server
void Stop()                     // Stop server
bool IsRunning() const          // Check running status

void SetSecureMode(bool secure) // Set secure mode
void SetTimeout(int seconds)    // Set timeout
void SetMaxTransferSize(size_t size) // Set maximum transfer size

// Callback settings
void SetReadCallback(std::function<bool(const std::string&, std::vector<uint8_t>&)> callback)
void SetWriteCallback(std::function<bool(const std::string&, const std::vector<uint8_t>&)> callback)
```

#### OpCode

```cpp
enum class OpCode : uint16_t {
    kReadRequest = 1,
    kWriteRequest = 2,
    kData = 3,
    kAcknowledge = 4,
    kError = 5
}
```

#### ErrorCode

```cpp
enum class ErrorCode : uint16_t {
    kNotDefined = 0,
    kFileNotFound = 1,
    kAccessViolation = 2,
    kDiskFull = 3,
    kIllegalOperation = 4,
    kUnknownTransferId = 5,
    kFileExists = 6,
    kNoSuchUser = 7
}
```

#### TransferMode

```cpp
enum class TransferMode {
    kNetAscii,
    kOctet,
    kMail
}
```

## Logging

### Log Levels

```cpp
enum LogLevel {
    kLogTrace = 0,     // Most detailed
    kLogDebug = 1,     // Debug information
    kLogInfo = 2,      // General information
    kLogWarn = 3,      // Warning
    kLogError = 4,     // Error
    kLogCritical = 5   // Critical error
}
```

### Log Configuration and Usage

```cpp
#include <tftp/tftp_logger.h>

// Set log level
TftpLogger::GetInstance().SetLogLevel(kLogInfo);

// Set log file
TftpLogger::GetInstance().SetLogFile("tftp_server.log");

// Log output
TFTP_INFO("Server started: port %d", port);
TFTP_WARN("Invalid access attempt: %s", client_ip.c_str());
TFTP_ERROR("File read error: %s", filename.c_str());
```

## Security

### Secure Mode

```cpp
// Enable secure mode (recommended)
server.SetSecureMode(true);
```

Secure mode enables the following protection features:

- Prevention of directory traversal attacks (`../` etc.)
- File path normalization
- Restriction of access outside permitted directories

### Recommendations

1. **Firewall**: Open only necessary ports
2. **Access Control**: Allow access only from trusted networks
3. **File Permissions**: Set appropriate file system permissions
4. **Log Monitoring**: Monitor suspicious access patterns

