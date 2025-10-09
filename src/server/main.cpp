#include "SocketManager.h"
#include <Log.h>
#include "../common/PerformanceMonitor.h"
#include <iostream>
#include <string>
#include <signal.h>
#include <atomic>
#include <cstdlib> // For exit

#ifndef _WIN32
#include <unistd.h>  // For getpid() on Unix
#include <sys/types.h>
#endif

using namespace Logger;

// Global pointer to SocketManager for signal handling
SocketManager* g_socketManager = nullptr;

// Atomic flags for shutdown control
std::atomic<bool> g_shutdownInProgress(false);
std::atomic<bool> g_forceExit(false);

// Signal handler for graceful shutdown
void signalHandler(int signum) {
    log_info(std::format("Received signal: {}", signum));
    
    // If we received multiple signals or the force exit flag is set, exit immediately
    if (g_shutdownInProgress.load() || g_forceExit.load()) {
        // If this is the second signal or we're already forcing an exit
        log_warnning("Forcing immediate exit");
        _exit(128 + signum); // Force immediate termination with no cleanup
    }
    
    // Set shutdown in progress flag
    g_shutdownInProgress.store(true);
    
    // Stop performance monitoring first
    log_info("Stopping performance monitoring...");
    PerformanceMonitor::getInstance().stopMonitoring();
    
    if (g_socketManager != nullptr) {
        log_info("Starting graceful shutdown...");
        g_socketManager->shutdown();
        
        // Setting the socket manager to nullptr after shutdown
        g_socketManager = nullptr;
    }
    
    // Set a force exit flag and start a timer
    std::thread([]() {
        // Wait 2 seconds for graceful shutdown
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // If we're still running, set force exit flag
        g_forceExit.store(true);
        log_warnning("Setting force exit flag");
        
        // Wait one more second and then force exit if still running
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_shutdownInProgress.load()) {
            log_error("Forced exit after timeout");
            _exit(1); // Force immediate termination with no cleanup
        }
    }).detach();
    
    // Exit immediately - don't wait for the main loop
    exit(0);
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
    log_info("Registering signal handlers");
    signal(SIGINT, signalHandler);  // Handle Ctrl+C
    signal(SIGTERM, signalHandler); // Handle termination request
#ifndef _WIN32
    signal(SIGHUP, signalHandler);  // Handle terminal disconnect (not on Windows)
#endif
    
    // Start performance monitoring
    PerformanceMonitor::getInstance().startMonitoring(30); // Report every 30 seconds
    log_info("Performance monitoring started");
    
    // Initialize SocketManager
    SocketManager socketManager;
    // Set global pointer for signal handler to access
    g_socketManager = &socketManager;
    
    // Initialize server
    socketManager.createSocket();
    socketManager.bindToPort(6001);
    socketManager.listenForConnections();
    
    log_info("Server started successfully with lock-free queues");
    log_info("Press Ctrl+C to shut down the server gracefully");
    
    // Main server loop with clean exit condition
    while (socketManager.isRunning() && !g_forceExit.load()) {
        socketManager.acceptConnection();
        
        // Small sleep to prevent CPU saturation
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Check for force exit flag
        if (g_forceExit.load()) {
            log_warnning("Force exit flag detected in main loop");
            break;
        }
    }
    
    // Additional cleanup before exiting
    log_info("Main loop exited, performing final cleanup...");
    
    // Stop performance monitoring if still running
    PerformanceMonitor::getInstance().stopMonitoring();
    
    // Reset global pointer
    g_socketManager = nullptr;
    
    // Reset shutdown flag
    g_shutdownInProgress.store(false);
    
    log_info("Server terminated successfully");
    return 0;

}
#endif
