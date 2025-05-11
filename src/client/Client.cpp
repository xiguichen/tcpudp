#include "Client.h"
#include <fstream>
#include <nlohmann/json.hpp> // Include the nlohmann/json library
#include <Log.h>
#include <stdlib.h>

using json = nlohmann::json;

Client::Client(const std::string& configFile)
{
    loadConfig(configFile);
    socketManager = std::make_shared<SocketManager>(localHostUdpPort, peerTcpPort, peerAddress);
}

void Client::loadConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    if(!file)
    {
        Logger::Log::getInstance().error("configuration file could not be found, program will exit");
        std::exit(EXIT_FAILURE);
    }
    json config;
    file >> config;
    localHostUdpPort = config["localHostUdpPort"].get<int>();
    peerTcpPort = config["peerTcpPort"].get<int>();
    peerAddress = config["peerAddress"].get<std::string>();
    clientId = config["clientId"].get<uint32_t>();

}

void Client::configure() {
    socketManager->manageSockets();
}

