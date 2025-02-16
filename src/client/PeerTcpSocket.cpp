#include "PeerTcpSocket.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

PeerTcpSocket::PeerTcpSocket(const std::string& address, int port) {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    connect(address, port);
}

void PeerTcpSocket::connect(const std::string& address, int port) {
    peerAddress.sin_family = AF_INET;
    peerAddress.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &peerAddress.sin_addr);
    ::connect(socketFd, (struct sockaddr*)&peerAddress, sizeof(peerAddress));
}

void PeerTcpSocket::send(const std::string& data) {
    // Send data
}

std::string PeerTcpSocket::receive() {
    // Receive data
    return "";
}
