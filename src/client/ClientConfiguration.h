#pragma once
#include <nlohmann/json.hpp> // Include the nlohmann/json library
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

  private:
    ClientConfiguration() = default;

    void LoadJsonConfig();

    nlohmann::json configJson = nullptr;
    const char *peerAddress = "peerAddress";
    const char *peerTcpPort = "peerTcpPort";
    const char *localHostUdpPort = "localHostUdpPort";
};
