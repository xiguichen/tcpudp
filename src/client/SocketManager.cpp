#include "SocketManager.h"

SocketManager::SocketManager(int udpPort, int tcpPort, const std::string& address)
    : localUdpSocket(udpPort), peerTcpSocket(address, tcpPort) {}

SocketManager::~SocketManager() {
    cleanupResources();
}

void SocketManager::manageSockets() {
    // Start threads for reading and writing
}

void SocketManager::cleanupResources() {
    // Clean up resources
}
