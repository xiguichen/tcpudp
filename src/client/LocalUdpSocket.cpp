#include "LocalUdpSocket.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

LocalUdpSocket::LocalUdpSocket(int port) {
  // Create a UDP socket
  socketFd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFd < 0) {
    std::cerr << "Failed to create socket" << std::endl;
    exit(EXIT_FAILURE);
  }

  // Bind the socket to the specified port
  bind(port);
}

void LocalUdpSocket::bind(int port) {
  memset(&localAddress, 0, sizeof(localAddress));
  localAddress.sin_family = AF_INET;
  localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  localAddress.sin_port = htons(port);

  if (::bind(socketFd, (struct sockaddr *)&localAddress, sizeof(localAddress)) <
      0) {
    std::cerr << "Failed to bind socket" << std::endl;
    exit(EXIT_FAILURE);
  }
}

void LocalUdpSocket::send(const std::vector<char> &data) {
    std::cout << "Sending data" << std::endl;
  ssize_t sentBytes =
      sendto(socketFd, data.data(), data.size(), 0,
             (struct sockaddr *)&localAddress, sizeof(localAddress));
  if (sentBytes < 0) {
    std::cerr << "Failed to send data" << std::endl;
  }
}

std::vector<char> LocalUdpSocket::receive() {
  std::vector<char> buffer(4096);
  socklen_t addrLen = sizeof(localAddress);

  // Use select to check if data is ready to be read
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(socketFd, &readfds);

  // Set timeout to NULL for blocking behavior
  int activity = select(socketFd + 1, &readfds, NULL, NULL, NULL);

  if (activity < 0) {
    std::cerr << "Select error" << std::endl;
    return {};
  }

  if (FD_ISSET(socketFd, &readfds)) {
    ssize_t receivedBytes =
        recvfrom(socketFd, buffer.data(), buffer.size(), MSG_WAITALL,
                 (struct sockaddr *)&localAddress, &addrLen);
    std::cout << "address: " << localAddress.sin_addr.s_addr << std::endl;
    std::cout << "port: " << localAddress.sin_port << std::endl;
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
