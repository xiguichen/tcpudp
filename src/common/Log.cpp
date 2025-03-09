#include "Log.h"

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

std::string Log::getLogLevelString(LogLevel level) {
  switch (level) {
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::WARNING:
    return "WARNING";
  case LogLevel::ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

void Log::info(const std::string &message) { log(LogLevel::INFO, message); }

void Log::warning(const std::string &message) {
  log(LogLevel::WARNING, message);
}

void Log::error(const std::string &message) { log(LogLevel::ERROR, message); }

void Log::debug(const std::string &message) {
  log(LogLevel::INFO, "DEBUG: " + message); // Assuming debug is a type of info
}

} // namespace Logger
