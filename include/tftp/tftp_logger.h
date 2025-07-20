/**
 * @file tftp_logger.h
 * @brief TFTPサーバーのロギング機能
 */

#ifndef TFTP_LOGGER_H_
#define TFTP_LOGGER_H_

#include "tftp/tftp_common.h"
#include <string>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tftpserver {

// ログレベル定義
enum LogLevel {
    kLogTrace = 0,
    kLogDebug = 1,
    kLogInfo = 2,
    kLogWarn = 3,
    kLogError = 4,
    kLogCritical = 5
};

/**
 * @class TftpLogger
 * @brief TFTPサーバーのロギング機能を提供するクラス
 */
class TFTP_EXPORT TftpLogger {
public:
    /**
     * @brief シングルトンインスタンスを取得
     * @return TftpLoggerインスタンス
     */
    static TftpLogger& GetInstance();

    /**
     * @brief デストラクタ
     */
    ~TftpLogger();

    /**
     * @brief ログファイルを設定
     * @param filename ログファイル名
     */
    void SetLogFile(const std::string& filename);

    /**
     * @brief ログレベルを設定
     * @param level ログレベル
     */
    void SetLogLevel(int level);

    /**
     * @brief ログメッセージを出力
     * @param level ログレベル
     * @param message ログメッセージ
     */
    void Log(int level, const std::string& message);

    /**
     * @brief フォーマット付きログメッセージを出力
     * @param level ログレベル
     * @param format フォーマット文字列
     * @param ... 可変引数
     */
    template<typename... Args>
    void LogFormat(int level, const char* format, Args... args) {
        if (level <= log_level_) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), format, args...);
            Log(level, buffer);

#ifdef _WIN32
            // Windows固有のデバッグ出力（OutputDebugStringAを使用）
            char debug_buffer[1024];
            const char* level_str = "INFO";
            switch (level) {
                case kLogTrace: level_str = "TRACE"; break;
                case kLogDebug: level_str = "DEBUG"; break;
                case kLogInfo: level_str = "INFO"; break;
                case kLogWarn: level_str = "WARN"; break;
                case kLogError: level_str = "ERROR"; break;
                case kLogCritical: level_str = "CRITICAL"; break;
            }
            snprintf(debug_buffer, sizeof(debug_buffer), "[%s] %s\n", level_str, buffer);
            OutputDebugStringA(debug_buffer);
#endif
        }
    }

private:
    TftpLogger() : log_level_(kLogInfo) {}
    TftpLogger(const TftpLogger&) = delete;
    TftpLogger& operator=(const TftpLogger&) = delete;

    int log_level_;
    std::ofstream log_file_;
    std::mutex mutex_;
};



// ロギングマクロ
#define TFTP_TRACE(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogTrace, __VA_ARGS__)
#define TFTP_DEBUG(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogDebug, __VA_ARGS__)
#define TFTP_INFO(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogInfo, __VA_ARGS__)
#define TFTP_WARN(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogWarn, __VA_ARGS__)
#define TFTP_ERROR(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogError, __VA_ARGS__)
#define TFTP_CRITICAL(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogCritical, __VA_ARGS__)

} // namespace tftpserver

#endif // TFTP_LOGGER_H_ 