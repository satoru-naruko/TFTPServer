/**
 * @file tftp_validation.h
 * @brief Input validation utilities for TFTP API parameters
 */

#ifndef TFTP_VALIDATION_H_
#define TFTP_VALIDATION_H_

#include "tftp/tftp_common.h"
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace tftpserver {
namespace validation {

// Validation constants
constexpr int kMinTimeout = 1;              // Minimum timeout in seconds
constexpr int kMaxTimeout = 3600;           // Maximum timeout in seconds (1 hour)
constexpr uint16_t kMinPort = 1;            // Minimum valid port number
constexpr uint16_t kMaxPort = 65535;        // Maximum valid port number
constexpr size_t kMinTransferSize = 512;    // Minimum transfer size (one TFTP block)
constexpr size_t kMaxTransferSize = 1024 * 1024 * 1024; // Maximum transfer size (1GB)
constexpr size_t kMaxPathLength = 4096;     // Maximum path length
constexpr size_t kMaxHostnameLength = 253;  // Maximum hostname length (RFC 1035)

/**
 * @brief Validates root directory path
 * @param root_dir Root directory path to validate
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateRootDirectory(const std::string& root_dir);

/**
 * @brief Validates port number
 * @param port Port number to validate
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidatePort(uint16_t port);

/**
 * @brief Validates timeout value
 * @param timeout_seconds Timeout in seconds to validate
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateTimeout(int timeout_seconds);

/**
 * @brief Validates transfer size
 * @param size Transfer size to validate
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateTransferSize(size_t size);

/**
 * @brief Validates hostname or IP address
 * @param host Hostname or IP address to validate
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateHost(const std::string& host);

/**
 * @brief Validates filename
 * @param filename Filename to validate
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateFilename(const std::string& filename);

/**
 * @brief Validates transfer mode
 * @param mode Transfer mode to validate
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateTransferMode(TransferMode mode);

/**
 * @brief Validates callback function (not null)
 * @param callback Callback function to validate
 * @return true if valid, false otherwise
 */
template<typename T>
bool ValidateCallback(const std::function<T>& callback) {
    return static_cast<bool>(callback);
}

/**
 * @brief Validates string parameter (not empty, within length limits)
 * @param str String to validate
 * @param max_length Maximum allowed length
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateString(const std::string& str, size_t max_length = kMaxStringLength);

/**
 * @brief Validates data buffer (not excessively large)
 * @param data Data buffer to validate
 * @param max_size Maximum allowed size
 * @return true if valid, false otherwise
 */
TFTP_EXPORT bool ValidateDataBuffer(const std::vector<uint8_t>& data, size_t max_size = kMaxTransferSize);

} // namespace validation
} // namespace tftpserver

#endif // TFTP_VALIDATION_H_