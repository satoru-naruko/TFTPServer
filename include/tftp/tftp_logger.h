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

// Production build log level configuration
#ifndef TFTP_LOG_LEVEL
    #if defined(TFTP_MINIMAL_LOGGING)
        #define TFTP_LOG_LEVEL kLogError    // Minimal: Only errors and critical
    #elif defined(NDEBUG) || defined(TFTP_PRODUCTION_BUILD)
        #define TFTP_LOG_LEVEL kLogWarn     // Production: Warnings, errors, and critical
    #elif defined(_DEBUG) || defined(DEBUG)
        #define TFTP_LOG_LEVEL kLogDebug    // Debug: Include debug messages
    #else
        #define TFTP_LOG_LEVEL kLogInfo     // Default: Include info messages
    #endif
#endif

// Compile-time log filtering to eliminate overhead in production
#define TFTP_LOG_ENABLED(level) (level >= TFTP_LOG_LEVEL)

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
     * @brief Check if log should be output for given level
     * @param level Log level to check
     * @return true if logging should occur for this level
     */
    bool ShouldLog(int level) const {
        return level >= log_level_;
    }

    /**
     * @brief Get current log level
     * @return Current log level
     */
    int GetLogLevel() const {
        return log_level_;
    }

    /**
     * @brief Output formatted log message
     * @param level Log level
     * @param format Format string
     * @param ... Variable arguments
     */
    template<typename... Args>
    void LogFormat(int level, const char* format, Args... args) {
        if (level >= log_level_) {  // Changed from <= to >= for correct comparison
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
    TftpLogger() : log_level_(TFTP_LOG_LEVEL) {}  // Use build-time log level as default
    TftpLogger(const TftpLogger&) = delete;
    TftpLogger& operator=(const TftpLogger&) = delete;

    int log_level_;
    std::ofstream log_file_;
    std::mutex mutex_;
};



// Optimized logging macros with compile-time filtering
// These macros eliminate function calls and string formatting overhead 
// when the log level is disabled at compile time

#if TFTP_LOG_ENABLED(kLogTrace)
    #define TFTP_TRACE(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogTrace, __VA_ARGS__)
#else
    #define TFTP_TRACE(...) ((void)0)
#endif

#if TFTP_LOG_ENABLED(kLogDebug)
    #define TFTP_DEBUG(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogDebug, __VA_ARGS__)
#else
    #define TFTP_DEBUG(...) ((void)0)
#endif

#if TFTP_LOG_ENABLED(kLogInfo)
    #define TFTP_INFO(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogInfo, __VA_ARGS__)
#else
    #define TFTP_INFO(...) ((void)0)
#endif

#if TFTP_LOG_ENABLED(kLogWarn)
    #define TFTP_WARN(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogWarn, __VA_ARGS__)
#else
    #define TFTP_WARN(...) ((void)0)
#endif

#if TFTP_LOG_ENABLED(kLogError)
    #define TFTP_ERROR(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogError, __VA_ARGS__)
#else
    #define TFTP_ERROR(...) ((void)0)
#endif

#if TFTP_LOG_ENABLED(kLogCritical)
    #define TFTP_CRITICAL(...) tftpserver::TftpLogger::GetInstance().LogFormat(tftpserver::kLogCritical, __VA_ARGS__)
#else
    #define TFTP_CRITICAL(...) ((void)0)
#endif

// Alternative high-performance logging macros for critical path code
// These should be used sparingly in performance-sensitive sections
#define TFTP_LOG_IF(level, condition, ...) \
    do { if (TFTP_LOG_ENABLED(level) && (condition)) { \
        tftpserver::TftpLogger::GetInstance().LogFormat(level, __VA_ARGS__); \
    } } while(0)

// Convenience macro for runtime log level checking (for dynamic log levels)
#define TFTP_LOG_RUNTIME(level, ...) \
    do { if (tftpserver::TftpLogger::GetInstance().ShouldLog(level)) { \
        tftpserver::TftpLogger::GetInstance().LogFormat(level, __VA_ARGS__); \
    } } while(0)

} // namespace tftpserver

#endif // TFTP_LOGGER_H_ 