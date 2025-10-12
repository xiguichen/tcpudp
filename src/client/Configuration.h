#pragma once
#include <string>
#include <nlohmann/json.hpp> // Include the nlohmann/json library


class Configuration
{
  public:
    static Configuration *getInstance()
    {
        static Configuration instance;
        return &instance;
    }

    Configuration(const Configuration &) = delete;
    Configuration &operator=(const Configuration &) = delete;

    const std::string getSocketAddress() const;

    uint16_t getPortNumber() const;

  private:

    Configuration() = default;

    void LoadJsonConfig();

    nlohmann::json configJson = nullptr;
    const char* peerAddress = "peerAddress";
    const char* peerTcpPort = "peerTcpPort";
};
