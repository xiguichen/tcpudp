#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
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
        return currentLogLevel.load(std::memory_order_relaxed);
    }

    // Cheap level check used by the log macros. Keeping the (potentially
    // expensive) message construction inside the branch means filtered-out
    // levels cost one relaxed atomic load instead of a full std::format.
    bool isEnabled(LogLevel level) const
    {
        return level >= currentLogLevel.load(std::memory_order_relaxed);
    }

    // Get current log level as string
    std::string getCurrentLogLevelString() const
    {
        return getLogLevelString(getCurrentLogLevel());
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

    std::atomic<LogLevel> currentLogLevel{LogLevel::LOG_INFO};
    std::ofstream logFile;
    std::mutex logMutex;

    std::string getLogLevelString(LogLevel level) const;
};

} // namespace Logger
  //

#define ENABLE_LOGGING
#ifdef ENABLE_LOGGING
// The message is only evaluated when the level is enabled, so per-packet
// log_debug(std::format(...)) call sites are free when DEBUG is filtered out.
#define log_info(message)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        auto &loggerInstance_ = Logger::Log::getInstance();                                                            \
        if (loggerInstance_.isEnabled(Logger::LogLevel::LOG_INFO))                                                     \
            loggerInstance_.info(std::string(__FILE__), std::to_string(__LINE__), message);                            \
    } while (0)
#define log_warnning(message)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        auto &loggerInstance_ = Logger::Log::getInstance();                                                            \
        if (loggerInstance_.isEnabled(Logger::LogLevel::LOG_WARN))                                                     \
            loggerInstance_.warning(std::string(__FILE__), std::to_string(__LINE__), message);                         \
    } while (0)
#define log_error(message)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        auto &loggerInstance_ = Logger::Log::getInstance();                                                            \
        if (loggerInstance_.isEnabled(Logger::LogLevel::LOG_ERROR))                                                    \
            loggerInstance_.error(std::string(__FILE__), std::to_string(__LINE__), message);                           \
    } while (0)
#define log_debug(message)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        auto &loggerInstance_ = Logger::Log::getInstance();                                                            \
        if (loggerInstance_.isEnabled(Logger::LogLevel::LOG_DEBUG))                                                    \
            loggerInstance_.debug(std::string(__FILE__), std::to_string(__LINE__), message);                           \
    } while (0)
#else
#define log_info(message)
#define log_warnning(message)
#define log_error(message)
#define log_debug(message)
#endif
