#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

class ClientConfiguration
{
  public:
    static ClientConfiguration *getInstance()
    {
        static ClientConfiguration instance;
        return &instance;
    }

    ClientConfiguration(const ClientConfiguration &) = delete;
    ClientConfiguration &operator=(const ClientConfiguration &) = delete;

    const std::string getSocketAddress() const;
    uint16_t getPortNumber() const;
    const std::uint16_t getLocalHostUdpPort() const;
    uint32_t getClientId() const;

    void setSocketAddress(const std::string &addr);
    void setPortNumber(uint16_t port);
    void setLocalHostUdpPort(uint16_t port);
    void setClientId(uint32_t id);

  private:
    ClientConfiguration() = default;

    void LoadJsonConfig();

    nlohmann::json configJson = nullptr;
    const char *peerAddressKey = "peerAddress";
    const char *peerTcpPortKey = "peerTcpPort";
    const char *localHostUdpPortKey = "localHostUdpPort";
    const char *clientIdKey = "clientId";

    std::optional<std::string> cliPeerAddress;
    std::optional<uint16_t> cliPeerTcpPort;
    std::optional<uint16_t> cliLocalHostUdpPort;
    std::optional<uint32_t> cliClientId;
};
