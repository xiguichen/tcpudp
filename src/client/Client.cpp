#include "Client.h"
#include <fstream>
#include <nlohmann/json.hpp> // Include the nlohmann/json library

using json = nlohmann::json;

Client::Client(const std::string& configFile)
{
    loadConfig(configFile);
    socketManager = std::make_shared<SocketManager>(localHostUdpPort, peerTcpPort, peerAddress);
}

void Client::loadConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    json config;
    file >> config;
    localHostUdpPort = config["localHostUdpPort"].get<int>();
    peerTcpPort = config["peerTcpPort"].get<int>();
    peerAddress = config["peerAddress"].get<std::string>();
}

void Client::configure() {
    socketManager->manageSockets();
}

