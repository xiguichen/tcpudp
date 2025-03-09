#include "LocalUdpSocket.h"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <Socket.h>

LocalUdpSocket::LocalUdpSocket(int port) {
  // Create a UDP socket
  socketFd = socket(AF_INET, SOCK_DGRAM, 0);
  sendSocketFd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sendSocketFd < 0)
  {
      perror("Failed to create the socket for send data");
  }
  if (socketFd < 0) {
    std::cerr << "[LocalUdpSocket::LocalUdpSocket] Failed to create socket" << std::endl;
    exit(EXIT_FAILURE);
  }

  // Bind the socket to the specified port
  bind(port);
}

void LocalUdpSocket::bind(int port) {
  memset(&localAddress, 0, sizeof(localAddress));
  localAddress.sin_family = AF_INET;
  localAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
  localAddress.sin_port = htons(port);

  if (::bind(socketFd, (struct sockaddr *)&localAddress, sizeof(localAddress)) <
      0) {
    std::cerr << "[LocalUdpSocket::bind] Failed to bind socket" << std::endl;
    exit(EXIT_FAILURE);
  }
}

void LocalUdpSocket::send(const std::vector<char> &data) {
    std::cout << "[LocalUdpSocket::send] Sending data" << std::endl;
    std::cout << "[LocalUdpSocket::send] Data Length: " << data.size() << std::endl;
    std::cout << "[LocalUdpSocket::send] Data: " << std::string(data.begin(), data.end()) << std::endl;
    std::cout << "remoteAddressLength: " << remoteAddressLength << std::endl;
  ssize_t sentBytes =
      SendUdpData(socketFd, data.data(), data.size(), 0,
             (struct sockaddr *)&remoteAddress, remoteAddressLength);
    std::cout << "remoteAddress.addr: " << inet_ntoa(remoteAddress.sin_addr ) << std::endl;
    std::cout << "remoteAddress.sin_port: " << remoteAddress.sin_port << std::endl;
    std::cout << "remoteAddressLength: " << remoteAddressLength << std::endl;
  if (sentBytes < 0) {
    std::cerr << "[LocalUdpSocket::send] Failed to send data" << std::endl;
    perror("failed to send data");
  }
}

std::vector<char> LocalUdpSocket::receive() {
  std::vector<char> buffer(4096);
  socklen_t tempLen = sizeof(remoteAddress);

  // Use select to check if data is ready to be read
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(socketFd, &readfds);

  // Set timeout to NULL for blocking behavior
  int activity = select(socketFd + 1, &readfds, NULL, NULL, NULL);

  if (activity < 0) {
    std::cerr << "[LocalUdpSocket::receive] Select error" << std::endl;
    return {};
  }

  if (FD_ISSET(socketFd, &readfds)) {
    ssize_t receivedBytes =
        RecvUdpData(socketFd, buffer.data(), buffer.size(), MSG_WAITALL,
                 (struct sockaddr *)&remoteAddress, &tempLen);
    std::cout << "[LocalUdpSocket::receive] address: " << remoteAddress.sin_addr.s_addr << std::endl;
    std::cout << "[LocalUdpSocket::receive] port: " << remoteAddress.sin_port << std::endl;
    std::cout << "[LocalUdpSocket::receive] socketLength: " << tempLen << std::endl;
    remoteAddressLength = tempLen;
    if (receivedBytes < 0) {
      std::cerr << "Failed to receive data" << std::endl;
      perror("Failed to receive data");
      receivedBytes = 0;
    } else {
      std::cout << "Data Length: " << receivedBytes << std::endl;
    }
    buffer.resize(receivedBytes);
  }

  return buffer;
}
