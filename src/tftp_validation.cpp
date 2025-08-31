#include "tftp/tftp_validation.h"
#include "tftp/tftp_logger.h"
#include <filesystem>
#include <regex>
#include <cctype>
#include <sstream>

namespace tftpserver {
namespace validation {

bool ValidateRootDirectory(const std::string& root_dir) {
    // Check if empty
    if (root_dir.empty()) {
        TFTP_ERROR("Root directory cannot be empty");
        return false;
    }
    
    // Check length
    if (root_dir.length() > kMaxPathLength) {
        TFTP_ERROR("Root directory path too long: %zu > %zu", root_dir.length(), kMaxPathLength);
        return false;
    }
    
    // Check for null bytes (security)
    if (root_dir.find('\0') != std::string::npos) {
        TFTP_ERROR("Root directory contains null bytes");
        return false;
    }
    
    // Check if directory exists or can be created (basic validation)
    try {
        std::filesystem::path path(root_dir);
        if (!path.has_root_name() && !path.has_root_directory() && !path.is_absolute()) {
            // Relative path - this is allowed but log a warning
            TFTP_WARN("Root directory is relative path: %s", root_dir.c_str());
        }
        
        // Check for directory traversal attempts in the path itself
        std::string normalized = path.lexically_normal().string();
        if (normalized.find("..") != std::string::npos) {
            TFTP_ERROR("Root directory contains directory traversal: %s", root_dir.c_str());
            return false;
        }
        
    } catch (const std::exception& e) {
        TFTP_ERROR("Invalid root directory path: %s (%s)", root_dir.c_str(), e.what());
        return false;
    }
    
    return true;
}

bool ValidatePort(uint16_t port) {
    // Port 0 is invalid for binding
    if (port == 0) {
        TFTP_ERROR("Port number cannot be 0");
        return false;
    }
    
    // Ports 1-1023 are privileged on many systems (warning only)
    if (port < 1024) {
        TFTP_WARN("Using privileged port number: %d", port);
    }
    
    return true;
}

bool ValidateTimeout(int timeout_seconds) {
    if (timeout_seconds < kMinTimeout) {
        TFTP_ERROR("Timeout too small: %d < %d", timeout_seconds, kMinTimeout);
        return false;
    }
    
    if (timeout_seconds > kMaxTimeout) {
        TFTP_ERROR("Timeout too large: %d > %d", timeout_seconds, kMaxTimeout);
        return false;
    }
    
    return true;
}

bool ValidateTransferSize(size_t size) {
    if (size < kMinTransferSize) {
        TFTP_ERROR("Transfer size too small: %zu < %zu", size, kMinTransferSize);
        return false;
    }
    
    if (size > kMaxTransferSize) {
        TFTP_ERROR("Transfer size too large: %zu > %zu", size, kMaxTransferSize);
        return false;
    }
    
    return true;
}

bool ValidateHost(const std::string& host) {
    if (host.empty()) {
        TFTP_ERROR("Host cannot be empty");
        return false;
    }
    
    if (host.length() > kMaxHostnameLength) {
        TFTP_ERROR("Host name too long: %zu > %zu", host.length(), kMaxHostnameLength);
        return false;
    }
    
    // Check for null bytes
    if (host.find('\0') != std::string::npos) {
        TFTP_ERROR("Host contains null bytes");
        return false;
    }
    
    // Basic hostname/IP validation
    // Allow IPv4 addresses and hostnames
    try {
        // IPv4 pattern: basic check for dotted decimal notation
        std::regex ipv4_pattern(R"(^(\d{1,3}\.){3}\d{1,3}$)");
        if (std::regex_match(host, ipv4_pattern)) {
            // Additional check for valid IPv4 ranges (0-255)
            std::istringstream iss(host);
            std::string octet;
            while (std::getline(iss, octet, '.')) {
                int val = std::stoi(octet);
                if (val < 0 || val > 255) {
                    TFTP_ERROR("Invalid IPv4 address: %s", host.c_str());
                    return false;
                }
            }
            return true;
        }
        
        // Hostname pattern: basic validation
        std::regex hostname_pattern(R"(^[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?)*$)");
        if (std::regex_match(host, hostname_pattern)) {
            return true;
        }
        
        // If neither IPv4 nor hostname pattern matches, still allow it but warn
        TFTP_WARN("Host format not recognized, allowing anyway: %s", host.c_str());
        return true;
        
    } catch (const std::exception& e) {
        TFTP_ERROR("Error validating host '%s': %s", host.c_str(), e.what());
        return false;
    }
}

bool ValidateFilename(const std::string& filename) {
    if (filename.empty()) {
        TFTP_ERROR("Filename cannot be empty");
        return false;
    }
    
    if (filename.length() > kMaxFilenameLength) {
        TFTP_ERROR("Filename too long: %zu > %zu", filename.length(), kMaxFilenameLength);
        return false;
    }
    
    // Check for null bytes
    if (filename.find('\0') != std::string::npos) {
        TFTP_ERROR("Filename contains null bytes");
        return false;
    }
    
    // Check for directory traversal attempts
    if (filename.find("..") != std::string::npos) {
        TFTP_ERROR("Filename contains directory traversal: %s", filename.c_str());
        return false;
    }
    
    // Check for absolute paths (security concern)
    if (filename[0] == '/' || filename[0] == '\\') {
        TFTP_ERROR("Absolute paths not allowed: %s", filename.c_str());
        return false;
    }
    
    // Check for Windows drive letters
    if (filename.length() >= 2 && filename[1] == ':') {
        TFTP_ERROR("Drive letters not allowed: %s", filename.c_str());
        return false;
    }
    
    // Check for control characters
    for (char c : filename) {
        if (std::iscntrl(static_cast<unsigned char>(c))) {
            TFTP_ERROR("Filename contains control characters: %s", filename.c_str());
            return false;
        }
    }
    
    return true;
}

bool ValidateTransferMode(TransferMode mode) {
    switch (mode) {
        case TransferMode::kNetAscii:
        case TransferMode::kOctet:
        case TransferMode::kMail:
            return true;
        default:
            TFTP_ERROR("Invalid transfer mode: %d", static_cast<int>(mode));
            return false;
    }
}

bool ValidateString(const std::string& str, size_t max_length) {
    if (str.length() > max_length) {
        TFTP_ERROR("String too long: %zu > %zu", str.length(), max_length);
        return false;
    }
    
    // Check for null bytes
    if (str.find('\0') != std::string::npos) {
        TFTP_ERROR("String contains null bytes");
        return false;
    }
    
    return true;
}

bool ValidateDataBuffer(const std::vector<uint8_t>& data, size_t max_size) {
    if (data.size() > max_size) {
        TFTP_ERROR("Data buffer too large: %zu > %zu", data.size(), max_size);
        return false;
    }
    
    return true;
}

} // namespace validation
} // namespace tftpserver