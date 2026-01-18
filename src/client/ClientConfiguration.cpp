#include "ClientConfiguration.h"
#include "Log.h"
#include <fstream>
#include <nlohmann/json.hpp> // Include the nlohmann/json library
#include <string>

const std::string ClientConfiguration::getSocketAddress() const
{
    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (configJson[peerAddress].is_string())
    {
        return configJson[peerAddress].get<std::string>();
    }

    log_error("Invalid or missing 'peerAddress' in config.json");
    return "";
}


uint16_t ClientConfiguration::getPortNumber() const
{
    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (configJson[peerTcpPort].is_number_unsigned())
    {
        return configJson[peerTcpPort].get<uint16_t>();
    }

    log_error("Invalid or missing 'port' in config.json");
    return 0;
}

void ClientConfiguration::LoadJsonConfig()
{
    try
    {
        // Load and parse the configuration file
        std::ifstream configFile("config.json");
        if (configFile.is_open())
        {
            configFile >> configJson;
            configFile.close();
        }
        else
        {
            throw std::runtime_error("Could not open config.json");
        }
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(std::string("Error reading config.json: ") + e.what());
    }
}

const std::uint16_t ClientConfiguration::getLocalHostUdpPort() const
{

    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (configJson[localHostUdpPort].is_number_unsigned())
    {
        return configJson[localHostUdpPort].get<uint16_t>();
    }

    log_error("Invalid or missing 'port' in config.json");
    return 0;
}

uint32_t ClientConfiguration::getClientId() const
{
    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (configJson[clientIdKey].is_number_unsigned())
    {
        return configJson[clientIdKey].get<uint32_t>();
    }

    log_error("Invalid or missing 'clientId' in config.json");
    return 0;
}

