#include "Client.h"
#include "ClientConfiguration.h"
#include <Log.h>
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
    std::cout << "  --log-level=LEVEL       Set log level (DEBUG, INFO, WARNING, ERROR)" << std::endl;
    std::cout << "  --peer-address=ADDR     Server TCP address (default: from config.json)" << std::endl;
    std::cout << "  --peer-port=PORT        Server TCP port (default: from config.json)" << std::endl;
    std::cout << "  --local-udp-port=PORT   Local UDP bind port (default: from config.json)" << std::endl;
    std::cout << "  --client-id=ID          Client ID (default: from config.json)" << std::endl;
    std::cout << "  --help                  Display this help message" << std::endl;
}

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
        else if (arg.find("--peer-address=") == 0)
        {
            std::string addr = arg.substr(15);
            ClientConfiguration::getInstance()->setSocketAddress(addr);
        }
        else if (arg.find("--peer-port=") == 0)
        {
            int port = std::stoi(arg.substr(12));
            ClientConfiguration::getInstance()->setPortNumber(static_cast<uint16_t>(port));
        }
        else if (arg.find("--local-udp-port=") == 0)
        {
            int port = std::stoi(arg.substr(17));
            ClientConfiguration::getInstance()->setLocalHostUdpPort(static_cast<uint16_t>(port));
        }
        else if (arg.find("--client-id=") == 0)
        {
            uint32_t id = static_cast<uint32_t>(std::stoul(arg.substr(12)));
            ClientConfiguration::getInstance()->setClientId(id);
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
