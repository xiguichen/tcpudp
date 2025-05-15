#include "Client.h"
#include <fstream>
#include <nlohmann/json.hpp> // Include the nlohmann/json library
#include <Log.h>
#include <stdlib.h>
const int TCP_CONNECTION_NUMBER = 30;

using json = nlohmann::json;

Client::Client(const std::string& configFile)
{
    loadConfig(configFile);

    // Initialize peer tcp connections
    std::vector<std::pair<std::string, int>> peerConnections;
    for(int i = 0; i < TCP_CONNECTION_NUMBER; i++)
    {
        peerConnections.emplace_back(peerAddress, peerTcpPort);
    }

    // Initialize socket manager
    socketManager = std::make_shared<SocketManager>(localHostUdpPort, peerConnections);
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

