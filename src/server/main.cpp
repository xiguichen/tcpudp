#include "SocketManager.h"
#include <Log.h>
#include "../common/PerformanceMonitor.h"
#include <iostream>
#include <string>
#include <signal.h>
#include <atomic>

using namespace Logger;

// Global pointer to SocketManager for signal handling
SocketManager* g_socketManager = nullptr;

// Atomic flag to indicate shutdown is in progress
std::atomic<bool> g_shutdownInProgress(false);

// Signal handler for graceful shutdown
void signalHandler(int signum) {
    Log::getInstance().info(std::format("Received signal: {}", signum));
    
    // Prevent multiple shutdown attempts
    if (g_shutdownInProgress.exchange(true)) {
        Log::getInstance().warning("Shutdown already in progress. Ignoring signal.");
        return;
    }
    
    if (g_socketManager != nullptr) {
        Log::getInstance().info("Starting graceful shutdown...");
        g_socketManager->shutdown();
        
        // Setting the socket manager to nullptr after shutdown
        g_socketManager = nullptr;
    }
    
    // Don't call exit() here - let the main loop detect that isRunning() is false
    // and perform a clean exit
}

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
    
    // Register signal handlers for graceful shutdown
    Log::getInstance().info("Registering signal handlers");
    signal(SIGINT, signalHandler);  // Handle Ctrl+C
    signal(SIGTERM, signalHandler); // Handle termination request
#ifndef _WIN32
    signal(SIGHUP, signalHandler);  // Handle terminal disconnect (not on Windows)
#endif
    
    // Start performance monitoring
    PerformanceMonitor::getInstance().startMonitoring(30); // Report every 30 seconds
    Log::getInstance().info("Performance monitoring started");
    
    // Initialize SocketManager
    SocketManager socketManager;
    // Set global pointer for signal handler to access
    g_socketManager = &socketManager;
    
    // Initialize server
    socketManager.createSocket();
    socketManager.bindToPort(6001);
    socketManager.listenForConnections();
    socketManager.startTcpQueueToUdpThreadPool();
    socketManager.startUdpQueueToTcpThreadPool();
    
    Log::getInstance().info("Server started successfully with lock-free queues");
    Log::getInstance().info("Press Ctrl+C to shut down the server gracefully");
    
    // Main server loop with clean exit condition
    while (socketManager.isRunning()) {
        socketManager.acceptConnection();
        
        // Small sleep to prevent CPU saturation
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Additional cleanup before exiting
    Log::getInstance().info("Main loop exited, performing final cleanup...");
    
    // Reset global pointer
    g_socketManager = nullptr;
    
    Log::getInstance().info("Server terminated successfully");
    return 0;

}
#endif
