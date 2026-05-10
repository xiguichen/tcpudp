#include "Peer.h"
#include "Server.h"
#include "ServerConfiguration.h"
#include <Log.h>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <thread>

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace Logger;

#ifndef EXCLUDE_MAIN
Server server;

void signalHandler(int signum)
{
    log_info(std::format("Received signal: {}", signum));
    PeerManager::CloseAllSockets();
    log_info("Sleep for 4 seconds to allow cleanup");
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));
    server.stop();

    exit(0);
}
#endif

void printUsage(const char *programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --log-level=LEVEL       Set log level (DEBUG, INFO, WARNING, ERROR)" << std::endl;
    std::cout << "  --port=PORT             TCP listen port (default: 7001)" << std::endl;
    std::cout << "  --udp-target-port=PORT  UDP target port for outgoing data (default: same as --port)" << std::endl;
    std::cout << "  --help                  Display this help message" << std::endl;
}

#ifdef EXCLUDE_MAIN

#else

int main(int argc, char *argv[])
{
    LogLevel logLevel = LogLevel::LOG_INFO;

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
            std::string levelStr = arg.substr(12);
            logLevel = Log::stringToLogLevel(levelStr);
        }
        else if (arg.find("--port=") == 0)
        {
            int port = std::stoi(arg.substr(7));
            ServerConfiguration::getInstance()->setPortNumber(port);
        }
        else if (arg.find("--udp-target-port=") == 0)
        {
            int port = std::stoi(arg.substr(18));
            ServerConfiguration::getInstance()->setUdpTargetPort(port);
        }
    }

    Log::getInstance().setLogLevel(logLevel);
    std::cout << "Log level set to: " << Log::getInstance().getCurrentLogLevelString() << std::endl;

    log_info("Registering signal handlers");
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#ifndef _WIN32
    signal(SIGHUP, signalHandler);
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
