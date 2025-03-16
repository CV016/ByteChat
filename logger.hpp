#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

enum LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

// Forward declare the logging macros
class Logger;
void log_direct(LogLevel level, const char* message);

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    
    void setLogFile(const std::string& filename, bool truncate = false) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logFile.is_open()) {
            logFile.close();
        }
        
        // Open with truncation if requested
        std::ios_base::openmode mode = std::ios::app;
        if (truncate) {
            mode = std::ios::trunc;
        }
        
        logFile.open(filename, mode);
        
        if (truncate) {
            // Use std::cout directly instead of LOG_INFO
            std::cout << getCurrentTimeString() << " [INFO] Log file truncated and restarted" << std::endl;
            if (logFile.is_open()) {
                logFile << getCurrentTimeString() << " [INFO] Log file truncated and restarted" << std::endl;
            }
        }
    }
    
    void setLogLevel(LogLevel level) {
        currentLevel = level;
    }
    
    template<typename... Args>
    void log(LogLevel level, const std::string& format, Args... args) {
        if (level < currentLevel) return;
        
        std::lock_guard<std::mutex> lock(logMutex);
        
        std::string timeStr = getCurrentTimeString();
        std::stringstream ss;
        ss << timeStr << " [" << getLevelString(level) << "] ";
        
        // Format message with arguments
        char buffer[1024];
        if (sizeof...(args) > 0) {
            snprintf(buffer, sizeof(buffer), format.c_str(), args...);
        } else {
            snprintf(buffer, sizeof(buffer), "%s", format.c_str());
        }
        
        ss << buffer;
        
        // Log to file and console
        if (logFile.is_open()) {
            logFile << ss.str() << std::endl;
            logFile.flush();
        }
        std::cout << ss.str() << std::endl;
    }
    
private:
    Logger() : currentLevel(INFO) {}
    ~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    std::string getCurrentTimeString() {
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&nowTime), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << nowMs.count();
        return ss.str();
    }
    
    std::string getLevelString(LogLevel level) {
        switch (level) {
            case DEBUG: return "DEBUG";
            case INFO: return "INFO";
            case WARNING: return "WARNING";
            case ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
    
    std::ofstream logFile;
    std::mutex logMutex;
    LogLevel currentLevel;
};

// Define logging macros after the class definition
#define LOG_DEBUG(format, ...) Logger::getInstance().log(DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Logger::getInstance().log(INFO, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) Logger::getInstance().log(WARNING, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Logger::getInstance().log(ERROR, format, ##__VA_ARGS__)

#endif // LOGGER_HPP