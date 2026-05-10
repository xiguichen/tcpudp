#include "ServerConfiguration.h"

ServerConfiguration* ServerConfiguration::instance = nullptr;

ServerConfiguration* ServerConfiguration::getInstance() {
    if (instance == nullptr) {
        instance = new ServerConfiguration();
    }
    return instance;
}

int ServerConfiguration::getPortNumber() const {
    return portNumber;
}

void ServerConfiguration::setPortNumber(int port) {
    portNumber = port;
}

int ServerConfiguration::getUdpTargetPort() const {
    return udpTargetPort;
}

void ServerConfiguration::setUdpTargetPort(int port) {
    udpTargetPort = port;
}
