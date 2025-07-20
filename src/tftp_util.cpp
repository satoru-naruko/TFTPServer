#include "tftp/tftp_util.h"
#include "tftp/tftp_logger.h"
#include <filesystem>
#include <algorithm>

namespace tftpserver {
namespace util {

bool IsPathSecure(const std::string& path, const std::string& root_dir) {
    // パスのセキュリティチェック
    // 1. 絶対パスが指定されていないか
    // 2. 親ディレクトリへの参照（../）がないか
    // 3. root_dirの外部にアクセスしようとしていないか
    
    // 絶対パスのチェック
#ifdef _WIN32
    if (path.size() >= 2 && path[1] == ':') {
        TFTP_ERROR("セキュリティ違反: 絶対パスが指定されています: %s", path.c_str());
        return false;
    }
#else
    if (!path.empty() && path[0] == '/') {
        TFTP_ERROR("セキュリティ違反: 絶対パスが指定されています: %s", path.c_str());
        return false;
    }
#endif

    // 正規化されたパスを取得
    std::string normalized_path = NormalizePath(root_dir + "/" + path);
    std::string normalized_root = NormalizePath(root_dir);
    
    // ルートディレクトリ内に収まっているかチェック
    if (normalized_path.find(normalized_root) != 0) {
        TFTP_ERROR("セキュリティ違反: ルートディレクトリ外へのアクセス: %s", normalized_path.c_str());
        return false;
    }
    
    return true;
}

std::string NormalizePath(const std::string& path) {
    // パスを正規化する（.. や . を解決する）
    std::filesystem::path fs_path = path;
    
    try {
        // 可能ならばパスを正規化
        if (std::filesystem::exists(fs_path)) {
            return std::filesystem::canonical(fs_path).string();
        }
        
        // ファイルが存在しない場合は、親ディレクトリを正規化してからファイル名を追加
        auto parent = fs_path.parent_path();
        auto filename = fs_path.filename();
        
        if (std::filesystem::exists(parent)) {
            return (std::filesystem::canonical(parent) / filename).string();
        }
        
        // 親ディレクトリも存在しない場合は、パスを構築
        std::string result = fs_path.string();
        
        // ディレクトリ区切り文字を統一
        std::replace(result.begin(), result.end(), '\\', '/');
        
        // 連続するスラッシュを単一のスラッシュに置き換え
        size_t pos = 0;
        while ((pos = result.find("//", pos)) != std::string::npos) {
            result.replace(pos, 2, "/");
        }
        
        // 末尾のスラッシュを削除
        if (!result.empty() && result.back() == '/') {
            result.pop_back();
        }
        
        // 相対パスの正規化
        if (!result.empty() && result[0] != '/' && result[0] != '\\') {
            result = std::filesystem::current_path().string() + "/" + result;
        }
        
        return result;
    } catch (const std::exception& e) {
        TFTP_ERROR("パス正規化エラー: %s (%s)", path.c_str(), e.what());
        return path;  // エラー時は元のパスを返す
    }
}

} // namespace util
} // namespace tftpserver 