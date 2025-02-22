#include "UdpToTcpSocketMap.h"

void UdpToTcpSocketMap::mapSockets(int udpSocket, int tcpSocket) {
    socketMap[udpSocket] = tcpSocket;
}

int UdpToTcpSocketMap::retrieveMappedTcpSocket(int udpSocket) {
    return socketMap[udpSocket];
}
