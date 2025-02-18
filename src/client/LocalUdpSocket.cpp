#include "LocalUdpSocket.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>

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

void LocalUdpSocket::send(const std::vector<char>& data) {
    ::sendto(socketFd, data.data(), data.size(), 0, (struct sockaddr*)&localAddress, sizeof(localAddress));
}

std::vector<char> LocalUdpSocket::receive() {
    std::vector<char> buffer(1024); // Adjust buffer size as needed
    socklen_t addrLen = sizeof(localAddress);
    ssize_t bytesReceived = ::recvfrom(socketFd, buffer.data(), buffer.size(), 0, (struct sockaddr*)&localAddress, &addrLen);
    if (bytesReceived > 0) {
        buffer.resize(bytesReceived); // Resize to actual data received
    } else {
        buffer.clear(); // Clear buffer if no data received or error
    }
    return buffer;
}
