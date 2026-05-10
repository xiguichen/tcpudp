#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <string>

class ServerConfiguration
{
  private:
    static ServerConfiguration *instance;

    ServerConfiguration() = default;

    int portNumber = 7001;
    int udpTargetPort = 7001;

  public:
    static ServerConfiguration *getInstance();
    int getPortNumber() const;
    void setPortNumber(int port);
    int getUdpTargetPort() const;
    void setUdpTargetPort(int port);
};

#endif // CONFIGURATION_H
