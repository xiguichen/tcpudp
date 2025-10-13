#include "ServerConfiguration.h"

ServerConfiguration* ServerConfiguration::instance = nullptr;

ServerConfiguration* ServerConfiguration::getInstance() {
    if (instance == nullptr) {
        instance = new ServerConfiguration();
    }
    return instance;
}

int ServerConfiguration::getPortNumber() const {
    // Placeholder implementation
    return 7001;
}

