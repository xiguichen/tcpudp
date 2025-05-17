#include "PeerTcpSocket.h"
#include <Log.h>
#include <Socket.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <Protocol.h>
#include <cstring>
#include <format>
#include <vector>
#include <Protocol.h>

using namespace Logger;

PeerTcpSocket::PeerTcpSocket(const std::string &address, int port) {
  // Create socket with non-blocking mode
  socketFd = CreateSocket(AF_INET, SOCK_STREAM, 0);
  if (socketFd < 0) {
    Log::getInstance().error("Failed to create TCP socket");
    throw std::runtime_error("Failed to create TCP socket");
  }
  
  // Set socket to non-blocking mode
  if (SetSocketNonBlocking(socketFd) < 0) {
    Log::getInstance().error("Failed to set socket to non-blocking mode");
    SocketClose(socketFd);
    throw std::runtime_error("Failed to set socket to non-blocking mode");
  }
  
  this->connect(address, port);
}

void PeerTcpSocket::connect(const std::string &address, int port) {
  peerAddress.sin_family = AF_INET;
  peerAddress.sin_port = htons(port);
  inet_pton(AF_INET, address.c_str(), &peerAddress.sin_addr);
  
  // Connect with timeout
  int result = SocketConnectNonBlocking(socketFd, (struct sockaddr *)&peerAddress, sizeof(peerAddress), 5000); // 5 seconds timeout
  if (result < 0) {
    Log::getInstance().error(std::format("Failed to connect to {}:{}", address, port));
    SocketLogLastError();
    SocketClose(socketFd);
    throw std::runtime_error("Failed to connect to peer");
  }
  
  Log::getInstance().info(std::format("Connected to peer at {}:{}", address, port));
}

void PeerTcpSocket::send(const std::vector<char> &data) {
  // Send data with timeout
  ssize_t result = SendTcpDataNonBlocking(socketFd, data.data(), data.size(), 0, 5000); // 5 seconds timeout
  if (result != static_cast<ssize_t>(data.size())) {
    Log::getInstance().error(std::format("Failed to send data, sent {} of {} bytes", 
                                        (result > 0 ? result : 0), data.size()));
    if (result < 0) {
      SocketLogLastError();
    }
    throw std::runtime_error("Failed to send data to peer");
  }
}

std::vector<char> PeerTcpSocket::receive() {
  // Check if socket is readable with timeout
  if (!IsSocketReadable(socketFd, 1000)) { // 1 second timeout
    Log::getInstance().info("Socket not readable within timeout");
    return {};
  }

  // Receive header with timeout
  UvtHeader header;
  ssize_t lengthBytesReceived = RecvTcpDataWithSizeNonBlocking(socketFd, &header, HEADER_SIZE, 0, HEADER_SIZE, 5000); // 5 seconds timeout
  
  if (lengthBytesReceived != HEADER_SIZE) {
    if (lengthBytesReceived == SOCKET_ERROR_TIMEOUT) {
      Log::getInstance().info("Timeout while receiving header");
    } else if (lengthBytesReceived == SOCKET_ERROR_WOULD_BLOCK) {
      Log::getInstance().info("Would block while receiving header");
    } else if (lengthBytesReceived == SOCKET_ERROR_CLOSED) {
      Log::getInstance().error("Connection closed by peer");
    } else {
      Log::getInstance().error(std::format("Failed to receive header, error: {}", lengthBytesReceived));
      SocketLogLastError();
    }
    return {};
  }

  // Convert message length from network byte order to host byte order
  uint16_t messageLength = ntohs(header.size);
  Log::getInstance().info(std::format(
      "TCP -> Queue: Data ID {} , Data Length: {}, Data Checksum: {}",
      header.id, messageLength, header.checksum));

  // Prepare a buffer to receive the actual message
  std::vector<char> buffer(messageLength);

  // Receive message body with timeout
  ssize_t bytesReceived = RecvTcpDataWithSizeNonBlocking(socketFd, buffer.data(), messageLength, 0, messageLength, 5000); // 5 seconds timeout
  
  if (bytesReceived != static_cast<ssize_t>(messageLength)) {
    if (bytesReceived == SOCKET_ERROR_TIMEOUT) {
      Log::getInstance().info("Timeout while receiving message body");
    } else if (bytesReceived == SOCKET_ERROR_WOULD_BLOCK) {
      Log::getInstance().info("Would block while receiving message body");
    } else if (bytesReceived == SOCKET_ERROR_CLOSED) {
      Log::getInstance().error("Connection closed by peer");
    } else {
      Log::getInstance().error(std::format("Failed to receive message body, received {} of {} bytes", 
                                          (bytesReceived > 0 ? bytesReceived : 0), messageLength));
      SocketLogLastError();
    }
    return {};
  }
  
  // Verify checksum
  uint8_t checksum = xor_checksum((uint8_t *)buffer.data(), messageLength);
  if (checksum != header.checksum) {
    Log::getInstance().error("Tcp -> Queue: Checksum verify failed");
    return {};
  }

  return buffer;
}

PeerTcpSocket::~PeerTcpSocket() { 
  SocketClose(socketFd); 
}

void PeerTcpSocket::sendHandshake() { 
  MsgBind msgBind;
  msgBind.clientId = 1; // Set appropriate client ID
  
  // Send handshake with timeout
  ssize_t result = SendTcpDataNonBlocking(socketFd, "0.0.1", 5, 0, 5000); // 5 seconds timeout
  if (result != 5) {
    Log::getInstance().error("Failed to send handshake");
    if (result < 0) {
      SocketLogLastError();
    }
    throw std::runtime_error("Failed to send handshake");
  }
  
  Log::getInstance().info("Handshake sent successfully");
}
