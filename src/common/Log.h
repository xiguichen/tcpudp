#pragma once

#include <fstream>
#include <thread>
#include <string>
#include <format>

namespace Logger
{

enum class LogLevel : int
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

class Log
{
  public:
    static Log &getInstance();

    void setLogFile(const std::string &filename);
    void setLogLevel(LogLevel level);
    void log(LogLevel level, const std::string &message);
    void log(LogLevel level, const std::string &file, const std::string &line, const std::string &message);

    // Convert string to LogLevel
    static LogLevel stringToLogLevel(const std::string &levelStr);

    // Get current log level
    LogLevel getCurrentLogLevel() const
    {
        return currentLogLevel;
    }

    // Get current log level as string
    std::string getCurrentLogLevelString() const
    {
        return getLogLevelString(currentLogLevel);
    }

    // Specific log level functions
    void debug(const std::string &file, const std::string &line, const std::string &message);
    void info(const std::string &file, const std::string &line, const std::string &message);
    void warning(const std::string &file, const std::string &line, const std::string &message);
    void error(const std::string &file, const std::string &line, const std::string &message);

  private:
    Log() = default;
    ~Log() = default;
    Log(const Log &) = delete;
    Log &operator=(const Log &) = delete;

    LogLevel currentLogLevel = LogLevel::LOG_INFO;
    std::ofstream logFile;
    std::mutex logMutex;

    std::string getLogLevelString(LogLevel level) const;
};

} // namespace Logger
  //

#define ENABLE_LOGGING
#ifdef ENABLE_LOGGING
#define log_info(message) Logger::Log::getInstance().info(std::string(__FILE__), std::to_string(__LINE__), message)
#define log_warnning(message)                                                                                          \
    Logger::Log::getInstance().warning(std::string(__FILE__), std::to_string(__LINE__), message)
#define log_error(message) Logger::Log::getInstance().error(std::string(__FILE__), std::to_string(__LINE__), message)
#define log_debug(message) Logger::Log::getInstance().debug(std::string(__FILE__), std::to_string(__LINE__), message)
#else
#define log_info(message)
#define log_warnning(message)
#define log_error(message)
#define log_debug(message)
#endif
