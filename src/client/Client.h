#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include "SocketManager.h"

class Client {
public:
    Client(const std::string& configFile);
    void configure();

private:
    int localHostUdpPort;
    int peerTcpPort;
    std::string peerAddress;
    SocketManager socketManager;

    void loadConfig(const std::string& configFile);
};

#endif // CLIENT_H
