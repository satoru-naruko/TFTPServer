#include "tftp/tftp_util.h"
#include "tftp/tftp_logger.h"
#include <filesystem>
#include <algorithm>

namespace tftpserver {
namespace util {

bool IsPathSecure(const std::string& path, const std::string& root_dir) {
    try {
        // Input validation
        if (path.empty() || root_dir.empty()) {
            TFTP_ERROR("Security violation: Empty path or root directory");
            return false;
        }
        
        // Check for null bytes (security vulnerability)
        if (path.find('\0') != std::string::npos) {
            TFTP_ERROR("Security violation: Null byte in path: %s", path.c_str());
            return false;
        }
        
        // Check for absolute path
#ifdef _WIN32
        if (path.size() >= 2 && path[1] == ':') {
            TFTP_ERROR("Security violation: Absolute path specified: %s", path.c_str());
            return false;
        }
        if (path.size() >= 3 && (path.substr(0, 2) == "\\\\" || path.substr(0, 2) == "//")) {
            TFTP_ERROR("Security violation: UNC path specified: %s", path.c_str());
            return false;
        }
#else
        if (!path.empty() && path[0] == '/') {
            TFTP_ERROR("Security violation: Absolute path specified: %s", path.c_str());
            return false;
        }
#endif
        
        // Check for dangerous patterns BEFORE normalization
        const std::vector<std::string> dangerous_patterns = {
            "..", "..\\", "../", "\\..\\", "/..", 
            ".\\", "./", "\\.\\", "/./", 
            "~", "$", "%", 
            "<", ">", "|", "?", "*"
        };
        
        for (const auto& pattern : dangerous_patterns) {
            if (path.find(pattern) != std::string::npos) {
                TFTP_ERROR("Security violation: Dangerous pattern '%s' found in path: %s", 
                          pattern.c_str(), path.c_str());
                return false;
            }
        }
        
        // Normalize paths using canonical form
        std::filesystem::path root_path = std::filesystem::absolute(root_dir);
        std::filesystem::path target_path = root_path / path;
        
        // Resolve symbolic links and normalize
        std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root_path);
        std::filesystem::path canonical_target = std::filesystem::weakly_canonical(target_path);
        
        // Convert to string for comparison
        std::string canonical_root_str = canonical_root.string();
        std::string canonical_target_str = canonical_target.string();
        
        // Ensure trailing separator for directory boundary check
        if (!canonical_root_str.empty() && 
            canonical_root_str.back() != std::filesystem::path::preferred_separator) {
            canonical_root_str += std::filesystem::path::preferred_separator;
        }
        
        // Check if target is within root (proper directory boundary check)
        bool is_within_root = (canonical_target_str.size() >= canonical_root_str.size() &&
                              canonical_target_str.substr(0, canonical_root_str.size()) == canonical_root_str);
        
        if (!is_within_root) {
            TFTP_ERROR("Security violation: Access outside root directory. Root: %s, Target: %s", 
                      canonical_root_str.c_str(), canonical_target_str.c_str());
            return false;
        }
        
        // Additional check: ensure no parent directory traversal in relative portion
        std::filesystem::path relative_path = std::filesystem::relative(canonical_target, canonical_root);
        std::string relative_str = relative_path.string();
        
        if (relative_str.find("..") != std::string::npos) {
            TFTP_ERROR("Security violation: Parent directory reference in relative path: %s", 
                      relative_str.c_str());
            return false;
        }
        
        TFTP_DEBUG("Path security check passed. Root: %s, Target: %s, Relative: %s", 
                  canonical_root_str.c_str(), canonical_target_str.c_str(), relative_str.c_str());
        
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        TFTP_ERROR("Filesystem error in path security check: %s (path: %s, root: %s)", 
                  e.what(), path.c_str(), root_dir.c_str());
        return false; // Fail securely
    } catch (const std::exception& e) {
        TFTP_ERROR("Error in path security check: %s (path: %s, root: %s)", 
                  e.what(), path.c_str(), root_dir.c_str());
        return false; // Fail securely
    }
}

std::string NormalizePath(const std::string& path) {
    try {
        // Input validation
        if (path.empty()) {
            return path;
        }
        
        // Check for null bytes
        if (path.find('\0') != std::string::npos) {
            TFTP_ERROR("Security violation: Null byte in path during normalization: %s", path.c_str());
            return "";
        }
        
        std::filesystem::path fs_path = path;
        
        // Use weakly_canonical which doesn't require file existence
        std::filesystem::path normalized = std::filesystem::weakly_canonical(fs_path);
        
        return normalized.string();
        
    } catch (const std::filesystem::filesystem_error& e) {
        TFTP_ERROR("Filesystem error in path normalization: %s (path: %s)", e.what(), path.c_str());
        return ""; // Return empty string on error for security
    } catch (const std::exception& e) {
        TFTP_ERROR("Error in path normalization: %s (path: %s)", e.what(), path.c_str());
        return ""; // Return empty string on error for security
    }
}

} // namespace util
} // namespace tftpserver 