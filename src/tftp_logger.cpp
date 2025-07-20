#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <ctime>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "tftp/tftp_logger.h"


namespace tftpserver {

TftpLogger& TftpLogger::GetInstance() {
    static TftpLogger instance;
    return instance;
}

TftpLogger::~TftpLogger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void TftpLogger::SetLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
    }
    log_file_.open(filename, std::ios::app);
}

void TftpLogger::SetLogLevel(int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    log_level_ = level;
}

void TftpLogger::Log(int level, const std::string& message) {
    if (level < log_level_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    // Log level as string
    std::string level_str;
    switch (level) {
        case kLogTrace: level_str = "TRACE"; break;
        case kLogDebug: level_str = "DEBUG"; break;
        case kLogInfo: level_str = "INFO"; break;
        case kLogWarn: level_str = "WARN"; break;
        case kLogError: level_str = "ERROR"; break;
        case kLogCritical: level_str = "CRITICAL"; break;
        default: level_str = "UNKNOWN"; break;
    }

    // Format log message
    std::stringstream ss;
    
    // Use localtime_s for thread safety on Windows
    std::tm time_info;
#ifdef _WIN32
    localtime_s(&time_info, &time);
#else
    // On POSIX systems
    time_info = *std::localtime(&time);
#endif
    
    ss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count()
       << " [" << level_str << "] " << message << std::endl;

    // Output the log
    if (log_file_.is_open()) {
        log_file_ << ss.str();
        log_file_.flush();
    } else {
        std::cerr << ss.str();
    }
}
#ifdef _WIN32
void OutputDebugLog(const char* level, const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    snprintf(buf, sizeof(buf), "[SERVER %s] ", level);
    vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), format, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}
#endif

} // namespace tftpserver 
