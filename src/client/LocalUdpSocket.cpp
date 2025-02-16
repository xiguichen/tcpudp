#include "LocalUdpSocket.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

LocalUdpSocket::LocalUdpSocket(int port) {
    socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    bind(port);
}

void LocalUdpSocket::bind(int port) {
    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = INADDR_ANY;
    localAddress.sin_port = htons(port);
    ::bind(socketFd, (struct sockaddr*)&localAddress, sizeof(localAddress));
}

void LocalUdpSocket::send(const std::string& data) {
    // Send data
}

std::string LocalUdpSocket::receive() {
    // Receive data
    return "";
}
