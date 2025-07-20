/**
 * @file tftp_util.h
 * @brief TFTP utility functions
 */

#ifndef TFTP_UTIL_H_
#define TFTP_UTIL_H_

#include "tftp/tftp_common.h"
#include <string>
#include <vector>
#include <filesystem>

namespace tftpserver {
namespace util {

/**
 * @brief Check if path is secure
 * @param path Path to check
 * @param root_dir Root directory
 * @return true if safe, false otherwise
 */
TFTP_EXPORT bool IsPathSecure(const std::string& path, const std::string& root_dir);

/**
 * @brief Normalize path
 * @param path Path to normalize  
 * @return Normalized path
 */
TFTP_EXPORT std::string NormalizePath(const std::string& path);

} // namespace util
} // namespace tftpserver

#endif // TFTP_UTIL_H_ 