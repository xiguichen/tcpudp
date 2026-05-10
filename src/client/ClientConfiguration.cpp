#include "ClientConfiguration.h"
#include "Log.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

void ClientConfiguration::setSocketAddress(const std::string &addr)
{
    cliPeerAddress = addr;
}

void ClientConfiguration::setPortNumber(uint16_t port)
{
    cliPeerTcpPort = port;
}

void ClientConfiguration::setLocalHostUdpPort(uint16_t port)
{
    cliLocalHostUdpPort = port;
}

void ClientConfiguration::setClientId(uint32_t id)
{
    cliClientId = id;
}

const std::string ClientConfiguration::getSocketAddress() const
{
    if (cliPeerAddress.has_value())
    {
        return cliPeerAddress.value();
    }

    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (!configJson.is_null() && configJson[peerAddressKey].is_string())
    {
        return configJson[peerAddressKey].get<std::string>();
    }

    log_error("Invalid or missing 'peerAddress' in config and no --peer-address provided");
    return "127.0.0.1";
}

uint16_t ClientConfiguration::getPortNumber() const
{
    if (cliPeerTcpPort.has_value())
    {
        return cliPeerTcpPort.value();
    }

    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (!configJson.is_null() && configJson[peerTcpPortKey].is_number_unsigned())
    {
        return configJson[peerTcpPortKey].get<uint16_t>();
    }

    log_error("Invalid or missing 'peerTcpPort' in config and no --peer-port provided");
    return 0;
}

const std::uint16_t ClientConfiguration::getLocalHostUdpPort() const
{
    if (cliLocalHostUdpPort.has_value())
    {
        return cliLocalHostUdpPort.value();
    }

    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (!configJson.is_null() && configJson[localHostUdpPortKey].is_number_unsigned())
    {
        return configJson[localHostUdpPortKey].get<uint16_t>();
    }

    log_error("Invalid or missing 'localHostUdpPort' in config and no --local-udp-port provided");
    return 0;
}

uint32_t ClientConfiguration::getClientId() const
{
    if (cliClientId.has_value())
    {
        return cliClientId.value();
    }

    if (configJson.is_null())
    {
        const_cast<ClientConfiguration *>(this)->LoadJsonConfig();
    }

    if (!configJson.is_null() && configJson[clientIdKey].is_number_unsigned())
    {
        return configJson[clientIdKey].get<uint32_t>();
    }

    log_error("Invalid or missing 'clientId' in config and no --client-id provided");
    return 0;
}

void ClientConfiguration::LoadJsonConfig()
{
    try
    {
        std::ifstream configFile("config.json");
        if (configFile.is_open())
        {
            configFile >> configJson;
            configFile.close();
            log_info("Loaded configuration from config.json");
        }
        else
        {
            log_info("config.json not found, using CLI arguments or defaults");
            configJson = nlohmann::json::object();
        }
    }
    catch (const std::exception &e)
    {
        log_warnning(std::string("Error reading config.json: ") + e.what());
        configJson = nlohmann::json::object();
    }
}
