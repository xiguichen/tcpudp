#include "Peer.h"
#include "Server.h"
#include <Log.h>
#include <cstdlib> // For exit
#include <iostream>
#include <signal.h>
#include <string>

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h> // For getpid() on Unix
#endif

using namespace Logger;

Server server;

// Signal handler for graceful shutdown
void signalHandler(int signum)
{
    log_info(std::format("Received signal: {}", signum));
    PeerManager::CloseAllSockets();
    server.stop();

    exit(0);
}

void printUsage(const char *programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --log-level=LEVEL   Set log level (DEBUG, INFO, WARNING, ERROR)" << std::endl;
    std::cout << "  --help              Display this help message" << std::endl;
}

#ifdef EXCLUDE_MAIN

#else

int main(int argc, char *argv[])
{
    // Default log level
    LogLevel logLevel = LogLevel::LOG_DEBUG;

    // Parse command line arguments
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg.find("--log-level=") == 0)
        {
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
    signal(SIGHUP, signalHandler); // Handle terminal disconnect (not on Windows)
#endif

    try
    {
        server.start();
    }
    catch (const std::exception &)
    {
        log_error("Exception caught in main, stopping server.");
        server.stop();
    }

    return 0;
}
#endif
