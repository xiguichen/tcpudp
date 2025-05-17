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
  // Create a UDP socket using our new function
  socketFd = CreateSocket(AF_INET, SOCK_DGRAM, 0);
  
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

  // Set socket to non-blocking mode
  if (SetSocketNonBlocking(socketFd) < 0) {
    std::cerr << "[LocalUdpSocket::LocalUdpSocket] Failed to set socket to non-blocking mode" << std::endl;
    SocketClose(socketFd);
    exit(EXIT_FAILURE);
  }
  
  Log::getInstance().info("Created non-blocking UDP socket");

  // Bind the socket to the specified port
  bind(port);
}

void LocalUdpSocket::bind(int port) {
  memset(&localAddress, 0, sizeof(localAddress));
  localAddress.sin_family = AF_INET;
  localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  localAddress.sin_port = htons(port);

  // Set socket option to allow address reuse
  int opt = 1;
  if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
    std::cerr << "[LocalUdpSocket::bind] Failed to set socket options" << std::endl;
    SocketClose(socketFd);
    exit(EXIT_FAILURE);
  }

  if (::bind(socketFd, (struct sockaddr *)&localAddress, sizeof(localAddress)) < 0) {
    std::cerr << "[LocalUdpSocket::bind] Failed to bind socket" << std::endl;
    SocketLogLastError();
    SocketClose(socketFd);
    exit(EXIT_FAILURE);
  }
  
  Log::getInstance().info(std::format("UDP socket bound to port {}", port));
}

void LocalUdpSocket::send(const std::vector<char> &data) {
  // Send data with timeout
  ssize_t sentBytes = SendUdpDataNonBlocking(socketFd, data.data(), data.size(), 0,
                                           (struct sockaddr *)&localAddress, sizeof(localAddress), 1000); // 1 second timeout
  
  if (sentBytes < 0) {
    if (sentBytes == SOCKET_ERROR_TIMEOUT) {
      Log::getInstance().error("Timeout while sending UDP data");
    } else if (sentBytes == SOCKET_ERROR_WOULD_BLOCK) {
      Log::getInstance().error("Would block while sending UDP data");
    } else {
      Log::getInstance().error(std::format("Failed to send UDP data, error: {}", sentBytes));
      SocketLogLastError();
    }
  } else if (sentBytes != static_cast<ssize_t>(data.size())) {
    Log::getInstance().error(std::format("Partial UDP data sent: {} of {} bytes", sentBytes, data.size()));
  }
}

std::vector<char> LocalUdpSocket::receive() {
  std::vector<char> buffer(4096);
  socklen_t addrLen = sizeof(localAddress);

  // Check if socket is readable with a short timeout
  if (!IsSocketReadable(socketFd, 100)) { // 100ms timeout
    // No data available, return empty buffer
    return {};
  }

  // Receive data non-blocking with timeout
  ssize_t receivedBytes = RecvUdpDataNonBlocking(socketFd, buffer.data(), buffer.size(), 0,
                                               (struct sockaddr *)&localAddress, &addrLen, 1000); // 1 second timeout
  
  if (receivedBytes < 0) {
    if (receivedBytes == SOCKET_ERROR_TIMEOUT) {
      Log::getInstance().info("Timeout while receiving UDP data");
    } else if (receivedBytes == SOCKET_ERROR_WOULD_BLOCK) {
      Log::getInstance().info("Would block while receiving UDP data");
    } else {
      std::cerr << "Failed to receive UDP data, error: " << receivedBytes << std::endl;
      SocketLogLastError();
    }
    return {};
  }
  
  // Resize buffer to actual received bytes
  buffer.resize(receivedBytes);
  return buffer;
}

LocalUdpSocket::~LocalUdpSocket() { 
  if (!bClosed) {
    SocketClose(socketFd);
  }
}

void LocalUdpSocket::close() {
  if (!bClosed) {
    SocketClose(socketFd);
    bClosed = true;
    Log::getInstance().info("UDP socket closed");
  }
}

