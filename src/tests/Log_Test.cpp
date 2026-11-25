#include "../common/Log.h"
#include <gtest/gtest.h>
#include <sstream>
#include <thread>
#include <vector>
#include <string>
#include <atomic>

class LogTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize log instance for each test
        Logger::Log::getInstance().setLogFile("test_log.txt");
        Logger::Log::getInstance().setLogLevel(Logger::LogLevel::LOG_DEBUG); // Set to DEBUG for detailed logging during analysis
    }

    void TearDown() override {
        // Clean up after tests
    }
};

TEST_F(LogTest, TestLogLevelConversion) {
    EXPECT_EQ(Logger::Log::stringToLogLevel("DEBUG"), Logger::LogLevel::LOG_DEBUG);
    EXPECT_EQ(Logger::Log::stringToLogLevel("INFO"), Logger::LogLevel::LOG_INFO);
    EXPECT_EQ(Logger::Log::stringToLogLevel("WARN"), Logger::LogLevel::LOG_WARN);
    EXPECT_EQ(Logger::Log::stringToLogLevel("ERROR"), Logger::LogLevel::LOG_ERROR);
}

TEST_F(LogTest, TestLogFileWriting) {
    Logger::Log::getInstance().info("LogTest", "42", "This is a test info log.");
    Logger::Log::getInstance().error("LogTest", "43", "This is a test error log.");

    // Assert log file content (simplified example)
    std::ifstream logFile("test_log.txt");
    std::stringstream buffer;
    buffer << logFile.rdbuf();
    std::string logContent = buffer.str();

    EXPECT_NE(logContent.find("INFO"), std::string::npos);
    EXPECT_NE(logContent.find("ERROR"), std::string::npos);
}

TEST_F(LogTest, TestCurrentLogLevel) {
    Logger::Log::getInstance().setLogLevel(Logger::LogLevel::LOG_WARN);
    EXPECT_EQ(Logger::Log::getInstance().getCurrentLogLevel(), Logger::LogLevel::LOG_WARN);
    EXPECT_EQ(Logger::Log::getInstance().getCurrentLogLevelString(), "WARNING");
}

// Hot paths call log_debug(std::format(...)). The argument must not be
// evaluated when DEBUG is filtered out, otherwise every packet pays for the
// format/string construction even though nothing is logged.
TEST_F(LogTest, FilteredDebugMessageIsNotEvaluated) {
    Logger::Log::getInstance().setLogLevel(Logger::LogLevel::LOG_INFO);

    int evaluations = 0;
    auto message = [&evaluations]() {
        ++evaluations;
        return std::string("expensive debug message");
    };

    log_debug(message());

    EXPECT_EQ(evaluations, 0)
        << "log_debug must not evaluate its argument when DEBUG is filtered out";
}
