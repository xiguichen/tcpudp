#ifndef SOCKETMANAGER_H
#define SOCKETMANAGER_H

#include <string>
#include "LocalUdpSocket.h"
#include "PeerTcpSocket.h"
#include "UdpToTcpQueue.h"
#include "TcpToUdpQueue.h"
#include <vector>

class SocketManager {
public:
    SocketManager(int udpPort, const std::vector<std::pair<std::string, int>>& peerConnections, uint32_t clientId = 1);
    ~SocketManager(); // Destructor declaration
    void manageSockets();
    void cleanupResources();

private:
    void localHostReadTask(bool& running);
    void localHostWriteTask(bool& running);
    void peerHostReadTask(bool& running, PeerTcpSocket& peerTcpSocket); 
    void peerHostWriteTask(bool& running, PeerTcpSocket& peerTcpSocket);
private:
    LocalUdpSocket localUdpSocket;
    std::vector<std::shared_ptr<PeerTcpSocket>> peerTcpSockets;
    UdpToTcpQueue udpToTcpQueue;
    TcpToUdpQueue tcpToUdpQueue;
};

#endif // SOCKETMANAGER_H
