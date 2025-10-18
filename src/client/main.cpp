#include "Client.h"
#include <Log.h>
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace Logger;

Client client;

void signalHandler(int signum)
{
    log_info(std::format("Received signal: {}", signum));

    client.Stop();
    SocketClearnup();
    exit(0);
}

void printUsage(const char *programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --log-level=LEVEL   Set log level (DEBUG, INFO, WARNING, ERROR)" << std::endl;
    std::cout << "  --help              Display this help message" << std::endl;
}

int main(int argc, char *argv[])
{
    // Default log level
    LogLevel logLevel = LogLevel::LOG_INFO;

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

    SocketInit();

    while (true)
    {
        try
        {
            log_info("Starting Client");
            client.Start();
            log_info("Client Stopped");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        catch (const std::exception &)
        {
            log_error("Exception in main loop, restarting client...");
            client.Stop();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    SocketClearnup();

    return 0;
}
