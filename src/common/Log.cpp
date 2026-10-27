#include "Log.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace Logger
{
namespace
{
std::string formatTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - std::chrono::system_clock::from_time_t(now_c))
                  .count();

    // localtime()/gmtime() return a pointer to a shared static buffer and are not
    // thread-safe; every worker thread logging at once would race on it.
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now_c);
#else
    localtime_r(&now_c, &local);
#endif

    std::stringstream ss;
    ss << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "." << std::setw(3)
       << std::setfill('0') << ms;
    return ss.str();
}
} // namespace

Log &Log::getInstance()
{
    static Log instance;
    return instance;
}

void Log::setLogFile(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open())
    {
        logFile.close();
    }
    logFile.open(filename, std::ios::out | std::ios::app);
}

void Log::setLogLevel(LogLevel level)
{
    // Read from any thread (the hot path checks getCurrentLogLevel()).
    currentLogLevel.store(level, std::memory_order_relaxed);
}

void Log::log(LogLevel level, const std::string &message)
{
    if (level < currentLogLevel.load(std::memory_order_relaxed))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(logMutex);

    std::string timestamp = formatTimestamp();

    // Format the log message with timestamp
    std::string formattedMessage = std::format("{} [{}] {}", timestamp, getLogLevelString(level), message);

    if (logFile.is_open())
    {
        logFile << formattedMessage << std::endl;
    }
    else
    {
        std::cout << formattedMessage << std::endl;
    }
}

void Log::log(LogLevel level, const std::string &file, const std::string &line, const std::string &message)
{
    if (level < currentLogLevel.load(std::memory_order_relaxed))
    {
        return;
    }
    std::lock_guard<std::mutex> lock(logMutex);

    std::string timestamp = formatTimestamp();

    // Format the log message with timestamp
    std::string formattedMessage =
        std::format("{}:{}: {} [{}] {}", file, line, timestamp, getLogLevelString(level), message);

    if (logFile.is_open())
    {
        logFile << formattedMessage << std::endl;
    }
    else
    {
        std::cout << formattedMessage << std::endl;
    }
}

std::string Log::getLogLevelString(LogLevel level) const
{
    switch (level)
    {
    case LogLevel::LOG_DEBUG:
        return "DEBUG";
    case LogLevel::LOG_INFO:
        return "INFO";
    case LogLevel::LOG_WARN:
        return "WARNING";
    case LogLevel::LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

LogLevel Log::stringToLogLevel(const std::string &levelStr)
{
    if (levelStr == "DEBUG" || levelStr == "debug")
    {
        return LogLevel::LOG_DEBUG;
    }
    else if (levelStr == "INFO" || levelStr == "info")
    {
        return LogLevel::LOG_INFO;
    }
    else if (levelStr == "WARNING" || levelStr == "warning" || levelStr == "WARN" || levelStr == "warn")
    {
        return LogLevel::LOG_WARN;
    }
    else if (levelStr == "ERROR" || levelStr == "error")
    {
        return LogLevel::LOG_ERROR;
    }
    else
    {
        // Default to INFO if unknown
        return LogLevel::LOG_INFO;
    }
}

void Log::debug(const std::string &file, const std::string &line, const std::string &message)
{
    log(LogLevel::LOG_DEBUG, file, line, message);
}

void Log::info(const std::string &file, const std::string &line, const std::string &message)
{
    log(LogLevel::LOG_INFO, file, line, message);
}

void Log::warning(const std::string &file, const std::string &line, const std::string &message)
{
    log(LogLevel::LOG_WARN, file, line, message);
}

void Log::error(const std::string &file, const std::string &line, const std::string &message)
{
    log(LogLevel::LOG_ERROR, file, line, message);
}

} // namespace Logger
