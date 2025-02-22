#include "TcpToUdpSocketMap.h"

void TcpToUdpSocketMap::mapSockets(int tcpSocket, int udpSocket) {
    socketMap[tcpSocket] = udpSocket;
}

int TcpToUdpSocketMap::retrieveMappedUdpSocket(int tcpSocket) {
    return socketMap[tcpSocket];
}
