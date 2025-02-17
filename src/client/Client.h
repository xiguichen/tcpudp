#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <memory>
#include "SocketManager.h"

class Client {
public:
    Client(const std::string& configFile);
    void configure();

private:
    int localHostUdpPort;
    int peerTcpPort;
    std::string peerAddress;
    std::shared_ptr<SocketManager> socketManager;

    void loadConfig(const std::string& configFile);
};

#endif // CLIENT_H

