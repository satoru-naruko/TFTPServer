/**
 * @file tftp_common.h
 * @brief Common definitions for TFTP protocol
 */

#ifndef TFTP_COMMON_H_
#define TFTP_COMMON_H_

#include <string>
#include <stdexcept>
#include <ostream>
#include <cstdint>

// DLL export/import definitions (for Windows)
#if defined(_MSC_VER) && defined(TFTP_SHARED_LIBRARY)
    #ifdef TFTP_BUILDING_LIBRARY
        #define TFTP_EXPORT __declspec(dllexport)
    #else
        #define TFTP_EXPORT __declspec(dllimport)
    #endif
#else
    #define TFTP_EXPORT
#endif

namespace tftpserver {

// TFTP protocol constants
constexpr uint16_t kDefaultTftpPort = 69;
constexpr size_t kMaxPacketSize = 516;  // 512 + 4 (header)
constexpr size_t kMaxDataSize = 512;
constexpr int kDefaultTimeout = 5;  // seconds

// Security limits for buffer overflow protection
constexpr size_t kMaxFilenameLength = 255;     // Maximum filename length
constexpr size_t kMaxOptionNameLength = 64;    // Maximum option name length  
constexpr size_t kMaxOptionValueLength = 64;   // Maximum option value length
constexpr size_t kMaxErrorMessageLength = 255; // Maximum error message length
constexpr size_t kMaxStringLength = 255;       // Maximum general string length
constexpr size_t kMaxOptionsCount = 16;        // Maximum number of options per packet
constexpr size_t kMinPacketSize = 4;           // Minimum valid packet size (opcode + data)

// Operation codes
enum class OpCode : uint16_t {
    kReadRequest = 1,
    kWriteRequest = 2,
    kData = 3,
    kAcknowledge = 4,
    kError = 5,
    kOACK = 6
};

// Error codes
enum class ErrorCode : uint16_t {
    kNotDefined = 0,
    kFileNotFound = 1,
    kAccessViolation = 2,
    kDiskFull = 3,
    kIllegalOperation = 4,
    kUnknownTransferId = 5,
    kFileExists = 6,
    kNoSuchUser = 7
};

// Transfer modes
enum class TransferMode {
    kNetAscii,
    kOctet,
    kMail
};

// Custom exception class
class TFTP_EXPORT TftpException : public std::runtime_error {
public:
    explicit TftpException(const std::string& message) : std::runtime_error(message) {}
};

// Stream output operators for Google Test
inline std::ostream& operator<<(std::ostream& os, OpCode op_code) {
    switch (op_code) {
        case OpCode::kReadRequest: return os << "ReadRequest";
        case OpCode::kWriteRequest: return os << "WriteRequest";
        case OpCode::kData: return os << "Data";
        case OpCode::kAcknowledge: return os << "Acknowledge";
        case OpCode::kError: return os << "Error";
        case OpCode::kOACK: return os << "OACK";
        default: return os << "Unknown(" << static_cast<int>(op_code) << ")";
    }
}

inline std::ostream& operator<<(std::ostream& os, ErrorCode error_code) {
    switch (error_code) {
        case ErrorCode::kNotDefined: return os << "NotDefined";
        case ErrorCode::kFileNotFound: return os << "FileNotFound";
        case ErrorCode::kAccessViolation: return os << "AccessViolation";
        case ErrorCode::kDiskFull: return os << "DiskFull";
        case ErrorCode::kIllegalOperation: return os << "IllegalOperation";
        case ErrorCode::kUnknownTransferId: return os << "UnknownTransferId";
        case ErrorCode::kFileExists: return os << "FileExists";
        case ErrorCode::kNoSuchUser: return os << "NoSuchUser";
        default: return os << "Unknown(" << static_cast<int>(error_code) << ")";
    }
}

// Equality comparison operators for Google Test
inline bool operator==(OpCode lhs, OpCode rhs) {
    return static_cast<int>(lhs) == static_cast<int>(rhs);
}

inline bool operator==(ErrorCode lhs, ErrorCode rhs) {
    return static_cast<int>(lhs) == static_cast<int>(rhs);
}

} // namespace tftpserver

#endif // TFTP_COMMON_H_ 