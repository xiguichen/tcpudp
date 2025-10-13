#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <string>
#include <set>
#include <cstdint>

class ServerConfiguration {
private:
    static ServerConfiguration* instance;
    // Private constructor
    ServerConfiguration() = default; 

    // Example client IDs
    std::set<uint32_t> allowedClientIds = {1, 2, 3};

public:
    static ServerConfiguration* getInstance();
    std::string getSocketAddress() const;
    int getPortNumber() const;

    // Get the list of client id that are allowed
    const std::set<uint32_t>& getAllowedClientIds() const {
        return allowedClientIds;
    }

};

#endif // CONFIGURATION_H

