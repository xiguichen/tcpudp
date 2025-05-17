#include "Client.h"
#include <Log.h>
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <chrono>
#include <thread>
#include <iostream>
#include <string>

using namespace Logger;

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --log-level=LEVEL   Set log level (DEBUG, INFO, WARNING, ERROR)" << std::endl;
    std::cout << "  --help              Display this help message" << std::endl;
}

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

#ifdef _WIN32
    WSADATA wsaData;
    int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaStartupResult != 0) {
        Log::getInstance().error("WSAStartup failed with error: " + std::to_string(wsaStartupResult));
        return 1;
    }
#endif

    while(true)
    {
        Log::getInstance().info("Starting Client");
        Client client("config.json");
        client.configure();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    #ifdef _WIN32
        WSACleanup();
    #endif
    
    return 0;
}
