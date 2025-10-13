#include "ServerConfiguration.h"

ServerConfiguration* ServerConfiguration::instance = nullptr;

ServerConfiguration* ServerConfiguration::getInstance() {
    if (instance == nullptr) {
        instance = new ServerConfiguration();
    }
    return instance;
}

std::string ServerConfiguration::getSocketAddress() const {
    // Placeholder implementation
    return "127.0.0.1";
}

int ServerConfiguration::getPortNumber() const {
    // Placeholder implementation
    return 7001;
}

