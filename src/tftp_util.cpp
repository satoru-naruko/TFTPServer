#include "tftp/tftp_util.h"
#include "tftp/tftp_logger.h"
#include <filesystem>
#include <algorithm>

namespace tftpserver {
namespace util {

bool IsPathSecure(const std::string& path, const std::string& root_dir) {
    // Path security check
    // 1. Check if absolute path is not specified
    // 2. Check if there are no parent directory references (../)
    // 3. Check if not trying to access outside root_dir
    
    // Check for absolute path
#ifdef _WIN32
    if (path.size() >= 2 && path[1] == ':') {
        TFTP_ERROR("Security violation: Absolute path specified: %s", path.c_str());
        return false;
    }
#else
    if (!path.empty() && path[0] == '/') {
        TFTP_ERROR("Security violation: Absolute path specified: %s", path.c_str());
        return false;
    }
#endif

    // Get normalized path
    std::string normalized_path = NormalizePath(root_dir + "/" + path);
    std::string normalized_root = NormalizePath(root_dir);
    
    // Check if within root directory
    if (normalized_path.find(normalized_root) != 0) {
        TFTP_ERROR("Security violation: Access outside root directory: %s", normalized_path.c_str());
        return false;
    }
    
    return true;
}

std::string NormalizePath(const std::string& path) {
    // Normalize path (resolve .. and .)
    std::filesystem::path fs_path = path;
    
    try {
        // Normalize path if possible
        if (std::filesystem::exists(fs_path)) {
            return std::filesystem::canonical(fs_path).string();
        }
        
        // If file doesn't exist, normalize parent directory and add filename
        auto parent = fs_path.parent_path();
        auto filename = fs_path.filename();
        
        if (std::filesystem::exists(parent)) {
            return (std::filesystem::canonical(parent) / filename).string();
        }
        
        // If parent directory also doesn't exist, build the path
        std::string result = fs_path.string();
        
        // Unify directory separators
        std::replace(result.begin(), result.end(), '\\', '/');
        
        // Replace consecutive slashes with single slash
        size_t pos = 0;
        while ((pos = result.find("//", pos)) != std::string::npos) {
            result.replace(pos, 2, "/");
        }
        
        // Remove trailing slash
        if (!result.empty() && result.back() == '/') {
            result.pop_back();
        }
        
        // Normalize relative path
        if (!result.empty() && result[0] != '/' && result[0] != '\\') {
            result = std::filesystem::current_path().string() + "/" + result;
        }
        
        return result;
    } catch (const std::exception& e) {
        TFTP_ERROR("Path normalization error: %s (%s)", path.c_str(), e.what());
        return path;  // Return original path on error
    }
}

} // namespace util
} // namespace tftpserver 