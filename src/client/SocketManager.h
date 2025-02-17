#ifndef SOCKETMANAGER_H
#define SOCKETMANAGER_H

#include <string>
#include "LocalUdpSocket.h"
#include "PeerTcpSocket.h"
#include "UdpToTcpQueue.h"
#include "TcpToUdpQueue.h"

class SocketManager {
public:
    SocketManager(int udpPort, int tcpPort, const std::string& address);
    ~SocketManager(); // Destructor declaration
    void manageSockets();
    void cleanupResources();

private:
    LocalUdpSocket localUdpSocket;
    PeerTcpSocket peerTcpSocket;
    UdpToTcpQueue udpToTcpQueue;
    TcpToUdpQueue tcpToUdpQueue;
};

#endif // SOCKETMANAGER_H
