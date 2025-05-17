#include "TcpToUdpSocketMap.h"

void TcpToUdpSocketMap::mapSockets(int tcpSocket, int udpSocket) {
    socketMap[tcpSocket] = udpSocket;
}

int TcpToUdpSocketMap::retrieveMappedUdpSocket(int tcpSocket) {
    auto it = socketMap.find(tcpSocket);
    if (it != socketMap.end()) {
        return it->second;
    }
    return -1; // Invalid socket
}
