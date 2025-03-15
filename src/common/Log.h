#pragma once

#include <string>
#include <fstream>
#include <mutex>

namespace Logger {

enum class LogLevel: int {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

class Log {
public:
    static Log& getInstance();

    void setLogFile(const std::string& filename);
    void setLogLevel(LogLevel level);
    void log(LogLevel level, const std::string& message);

    // Specific log level functions
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void debug(const std::string& message);

private:
    Log() = default;
    ~Log() = default;
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    LogLevel currentLogLevel = LogLevel::LOG_INFO;
    std::ofstream logFile;
    std::mutex logMutex;

    std::string getLogLevelString(LogLevel level);
};

} // namespace Logger
