#include "UDPToTCPSocketMap.h"

void UDPToTCPSocketMap::mapSockets(int udpSocket, int tcpSocket) {
    socketMap[udpSocket] = tcpSocket;
}

int UDPToTCPSocketMap::retrieveMappedTCPSocket(int udpSocket) {
    return socketMap[udpSocket];
}
