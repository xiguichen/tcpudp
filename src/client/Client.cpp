#include "Client.h"
#include <fstream>
#include <json/json.h> // Assuming you have a JSON library

Client::Client(const std::string& configFile) {
    loadConfig(configFile);
    socketManager = SocketManager(localHostUdpPort, peerTcpPort, peerAddress);
}

void Client::loadConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    Json::Value config;
    file >> config;
    localHostUdpPort = config["localHostUdpPort"].asInt();
    peerTcpPort = config["peerTcpPort"].asInt();
    peerAddress = config["peerAddress"].asString();
}

void Client::configure() {
    socketManager.manageSockets();
}
