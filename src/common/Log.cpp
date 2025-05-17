#include "Log.h"
#include <iostream>

namespace Logger {

Log &Log::getInstance() {
  static Log instance;
  return instance;
}

void Log::setLogFile(const std::string &filename) {
  std::lock_guard<std::mutex> lock(logMutex);
  if (logFile.is_open()) {
    logFile.close();
  }
  logFile.open(filename, std::ios::out | std::ios::app);
}

void Log::setLogLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(logMutex);
  currentLogLevel = level;
}

void Log::log(LogLevel level, const std::string &message) {
  if (level < currentLogLevel) {
    return;
  }
  std::lock_guard<std::mutex> lock(logMutex);
  if (logFile.is_open()) {
    logFile << "[" << getLogLevelString(level) << "] " << message << std::endl;
  } else {
    std::cout << "[" << getLogLevelString(level) << "] " << message << std::endl;
  }
}

std::string Log::getLogLevelString(LogLevel level) const {
  switch (level) {
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

LogLevel Log::stringToLogLevel(const std::string& levelStr) {
  if (levelStr == "DEBUG" || levelStr == "debug") {
    return LogLevel::LOG_DEBUG;
  } else if (levelStr == "INFO" || levelStr == "info") {
    return LogLevel::LOG_INFO;
  } else if (levelStr == "WARNING" || levelStr == "warning" || levelStr == "WARN" || levelStr == "warn") {
    return LogLevel::LOG_WARN;
  } else if (levelStr == "ERROR" || levelStr == "error") {
    return LogLevel::LOG_ERROR;
  } else {
    // Default to INFO if unknown
    return LogLevel::LOG_INFO;
  }
}

void Log::debug(const std::string &message) { log(LogLevel::LOG_DEBUG, message); }

void Log::info(const std::string &message) { log(LogLevel::LOG_INFO, message); }

void Log::warning(const std::string &message) {
  log(LogLevel::LOG_WARN, message);
}

void Log::error(const std::string &message) { log(LogLevel::LOG_ERROR, message); }

} // namespace Logger
