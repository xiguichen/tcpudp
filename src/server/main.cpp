#include "SocketManager.h"
#include <Log.h>
#include "../common/PerformanceMonitor.h"
#include <iostream>
#include <string>

using namespace Logger;

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --log-level=LEVEL   Set log level (DEBUG, INFO, WARNING, ERROR)" << std::endl;
    std::cout << "  --help              Display this help message" << std::endl;
}

#ifdef EXCLUDE_MAIN

#else

int main(int argc, char* argv[]) {
    // Default log level
    LogLevel logLevel = LogLevel::LOG_INFO;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg.find("--log-level=") == 0) {
            std::string levelStr = arg.substr(12); // Extract level after '='
            logLevel = Log::stringToLogLevel(levelStr);
        }
    }
    
    Log::getInstance().setLogLevel(logLevel);
    std::cout << "Log level set to: " << Log::getInstance().getCurrentLogLevelString() << std::endl;
    
    // Start performance monitoring
    PerformanceMonitor::getInstance().startMonitoring(30); // Report every 30 seconds
    Log::getInstance().info("Performance monitoring started");
    
    SocketManager socketManager;
    socketManager.createSocket();
    socketManager.bindToPort(6001);
    socketManager.listenForConnections();
    socketManager.startTcpQueueToUdpThreadPool();
    socketManager.startUdpQueueToTcpThreadPool();
    
    Log::getInstance().info("Server started successfully with lock-free queues");
    
    while (true) {
        socketManager.acceptConnection();
    }

}
#endif
