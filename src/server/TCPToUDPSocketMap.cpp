#include "TCPToUDPSocketMap.h"

void TCPToUDPSocketMap::mapSockets(int tcpSocket, int udpSocket) {
    socketMap[tcpSocket] = udpSocket;
}

int TCPToUDPSocketMap::retrieveMappedUDPSocket(int tcpSocket) {
    return socketMap[tcpSocket];
}
