/**
 * @file tftp_logger.h
 * @brief TFTP server logging functionality
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

// Log level definitions
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
 * @brief Class that provides TFTP server logging functionality
 */
class TFTP_EXPORT TftpLogger {
public:
    /**
     * @brief Get singleton instance
     * @return TftpLogger instance
     */
    static TftpLogger& GetInstance();

    /**
     * @brief Destructor
     */
    ~TftpLogger();

    /**
     * @brief Set log file
     * @param filename Log filename
     */
    void SetLogFile(const std::string& filename);

    /**
     * @brief Set log level
     * @param level Log level
     */
    void SetLogLevel(int level);

    /**
     * @brief Output log message
     * @param level Log level
     * @param message Log message
     */
    void Log(int level, const std::string& message);

    /**
     * @brief Output formatted log message
     * @param level Log level
     * @param format Format string
     * @param ... Variable arguments
     */
    template<typename... Args>
    void LogFormat(int level, const char* format, Args... args) {
        if (level <= log_level_) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), format, args...);
            Log(level, buffer);

#ifdef _WIN32
            // Windows-specific debug output (using OutputDebugStringA)
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



// Logging macros
#define TFTP_TRACE(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogTrace, __VA_ARGS__)
#define TFTP_DEBUG(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogDebug, __VA_ARGS__)
#define TFTP_INFO(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogInfo, __VA_ARGS__)
#define TFTP_WARN(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogWarn, __VA_ARGS__)
#define TFTP_ERROR(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogError, __VA_ARGS__)
#define TFTP_CRITICAL(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogCritical, __VA_ARGS__)

} // namespace tftpserver

#endif // TFTP_LOGGER_H_ 