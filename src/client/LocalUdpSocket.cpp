#include "LocalUdpSocket.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <vector>
#include <Socket.h>
#include <Log.h>

using namespace Logger;

LocalUdpSocket::LocalUdpSocket(int port) {

  // Create a UDP socket
#ifndef _WIN32
  socketFd = socket(AF_INET, SOCK_DGRAM, 0);
#else
  socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif


#ifndef _WIN32
  if (socketFd < 0) {
    std::cerr << "[LocalUdpSocket::LocalUdpSocket] Failed to create socket" << std::endl;
    exit(EXIT_FAILURE);
  }
#else
  if (socketFd == INVALID_SOCKET) {
    std::cerr << "[LocalUdpSocket::LocalUdpSocket] Failed to create socket" << std::endl;
    exit(EXIT_FAILURE);
  }
#endif

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
    std::cerr << "[LocalUdpSocket::bind] Failed to bind socket" << std::endl;
    exit(EXIT_FAILURE);
  }
}

void LocalUdpSocket::send(const std::vector<char> &data) {
  ssize_t sentBytes =
      SendUdpData(socketFd, data.data(), data.size(), 0,
             (struct sockaddr *)&localAddress, sizeof(localAddress));
  if (sentBytes < 0) {
      Log::getInstance().error("Failed to send data.");
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
    std::cerr << "[LocalUdpSocket::receive] Select error" << std::endl;
    return {};
  }

  int flags = 0;

#ifndef _WIN32
  flags = MSG_WAITALL;
#endif

  if (FD_ISSET(socketFd, &readfds)) {
    ssize_t receivedBytes =
        RecvUdpData(socketFd, buffer.data(), buffer.size(), flags,
                 (struct sockaddr *)&localAddress, &addrLen);
    if (receivedBytes < 0) {
      std::cerr << "Failed to receive data" << std::endl;
      perror("Failed to receive data");
      receivedBytes = 0;
    } else {
    }
    buffer.resize(receivedBytes);
  }

  return buffer;
}
LocalUdpSocket::~LocalUdpSocket() { if(!bClosed) SocketClose(socketFd); }

void LocalUdpSocket::close() {
  if (!bClosed) {
    SocketClose(socketFd);
  }
}

