# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Development Environment Setup
```cmd
# Must be run first in each new terminal session
setup_dev_env.cmd

# Install dependencies (vcpkg-based)
vcpkg install
```

### Build System (CMake)
```cmd
# Configure (from project root)
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..

# Build
cmake --build . --config Debug

# Clean build when needed
rmdir /s /q build && mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --config Debug
```

### Command Aliases (after setup_dev_env.cmd)
```cmd
cmake_configure  # Configure CMake
cmake_build      # Build project
cmake_test       # Run tests
```

### Testing
```cmd
# Run all tests
ctest -C Debug --output-on-failure

# From build directory
ctest --output-on-failure
```

### Code Quality
```cmd
# Format code (Google C++ Style Guide)
clang-format -i -style=Google **/*.cpp **/*.h

# Static analysis
clang-tidy -checks=*,-fuchsia-* src/*.cpp
cppcheck --std=c++17 --enable=all src/
```

## Architecture

### Project Structure
- **include/tftp/**: Public API headers with Doxygen documentation
- **src/**: Implementation files following Google C++ Style Guide
- **src/internal/**: Private implementation details (pimpl pattern)
- **examples/**: Usage examples and demonstration code
- **tests/**: GoogleTest-based unit tests (minimum 80% coverage required)

### Core Components
- `TftpServer`: Main server class with customizable callbacks for file operations
- `TftpClient`: Client implementation (libcurl-based) for file transfers
- `TftpPacket`: TFTP protocol packet handling (RRQ/WRQ/DATA/ACK/ERROR)
- `TftpLogger`: Printf-based logging with levels (trace/debug/info/warn/error/critical)
- `TftpUtil`: Common utilities including network byte order conversion

### Key Design Patterns
- **Pimpl idiom**: Used for cross-platform abstraction and ABI stability
- **RAII**: Proper resource management with smart pointers
- **Callback-based**: Custom file I/O handlers for flexibility

### TFTP Protocol Implementation
- RFC 1350 compliant with standard packet types (OpCode 1-5)
- Support for both netascii and octet transfer modes
- Default 512-byte data blocks with configurable transfer sizes
- Comprehensive error handling with proper error codes (0-7)

### Threading and Concurrency
- Thread-safe server implementation
- Concurrent client request handling
- Proper synchronization for shared resources

## Development Guidelines

### Code Standards
- **Language**: C++17 (strictly enforced)
- **Style**: Google C++ Style Guide with clang-format
- **Naming**: Classes use UpperCamelCase, functions use UpperCamelCase(), variables use lower_snake_case
- **Member variables**: End with underscore (member_variable_)
- **Constants**: Use kUpperCamelCase prefix

### Memory Management
- Smart pointers by default (std::unique_ptr, std::shared_ptr)
- Raw pointers only for non-owning, nullable references
- RAII for all resource management

### Security Requirements
- Always validate external input (especially TFTP packets)
- Enable secure mode to prevent directory traversal attacks
- Never log sensitive information or key material
- Input validation for all file paths and network data

### Testing Requirements
- GoogleTest framework mandatory
- Minimum 80% statement coverage required
- 100% coverage for critical security paths
- All tests must compile and pass successfully

### Platform Support
- Primary target: Windows (MSVC 2022+)
- Secondary: Linux compatibility maintained
- CMake 3.20+ for cross-platform builds
- vcpkg for consistent dependency management