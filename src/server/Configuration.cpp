#include "Configuration.h"

Configuration* Configuration::instance = nullptr;

Configuration* Configuration::getInstance() {
    if (instance == nullptr) {
        instance = new Configuration();
    }
    return instance;
}

std::string Configuration::getSocketAddress() const {
    // Placeholder implementation
    return "127.0.0.1";
}

int Configuration::getPortNumber() const {
    // Placeholder implementation
    return 5002;
}

