#include "Client.h"
#include <fstream>
#include <nlohmann/json.hpp> // Include the nlohmann/json library
#include <iostream>

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
    std::cout << "localHostUdpPort: " << localHostUdpPort << std::endl;
    std::cout << "peerTcpPort: " << peerTcpPort << std::endl;
    std::cout << "peerAddress: " << peerAddress << std::endl;
}

void Client::configure() {
    socketManager->manageSockets();
}

