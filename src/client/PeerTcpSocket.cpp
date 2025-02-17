#include "PeerTcpSocket.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>

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

void PeerTcpSocket::send(const std::vector<char>& data) {
    ::send(socketFd, data.data(), data.size(), 0);
}

std::vector<char> PeerTcpSocket::receive() {
    std::vector<char> buffer(1024); // Adjust buffer size as needed
    ssize_t bytesReceived = ::recv(socketFd, buffer.data(), buffer.size(), 0);
    if (bytesReceived > 0) {
        buffer.resize(bytesReceived); // Resize to actual data received
    } else {
        buffer.clear(); // Clear buffer if no data received or error
    }
    return buffer;
}
