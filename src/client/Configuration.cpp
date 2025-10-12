#include "Configuration.h"
#include "Log.h"
#include <fstream>
#include <nlohmann/json.hpp> // Include the nlohmann/json library
#include <string>

const std::string Configuration::getSocketAddress() const
{
    if (configJson.is_null())
    {
        const_cast<Configuration *>(this)->LoadJsonConfig();
    }

    if (configJson[peerAddress].is_string())
    {
        return configJson[peerAddress].get<std::string>();
    }

    log_error("Invalid or missing 'peerAddress' in config.json");
    return "";
}


uint16_t Configuration::getPortNumber() const
{
    if (configJson.is_null())
    {
        const_cast<Configuration *>(this)->LoadJsonConfig();
    }

    if (configJson[peerTcpPort].is_number_unsigned())
    {
        return configJson[peerTcpPort].get<uint16_t>();
    }

    log_error("Invalid or missing 'port' in config.json");
    return 0;
}

void Configuration::LoadJsonConfig()
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
