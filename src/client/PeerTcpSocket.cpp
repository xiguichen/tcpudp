#include "PeerTcpSocket.h"
#include <Log.h>
#include <Socket.h>
#include <arpa/inet.h>
#include <format>
#include <iostream>
#include <unistd.h>
#include <vector>

using namespace Logger;

PeerTcpSocket::PeerTcpSocket(const std::string &address, int port) {
  socketFd = socket(AF_INET, SOCK_STREAM, 0);
  this->connect(address, port);
}

void PeerTcpSocket::connect(const std::string &address, int port) {
  peerAddress.sin_family = AF_INET;
  peerAddress.sin_port = htons(port);
  inet_pton(AF_INET, address.c_str(), &peerAddress.sin_addr);
  SocketConnect(socketFd, (struct sockaddr *)&peerAddress, sizeof(peerAddress));
}

void PeerTcpSocket::send(const std::vector<char> &data) {
  // send the data length first
  uint32_t dataLength = htonl(data.size());
  Log::getInstance().info(
      std::format("Queue -> TCP: Data Length {}", data.size()));
  SendTcpData(socketFd, &dataLength, sizeof(dataLength), 0);
  SendTcpData(socketFd, data.data(), data.size(), 0);
}

std::vector<char> PeerTcpSocket::receive() {
  // First, read the length of the message
  uint32_t messageLength = 0;
  ssize_t lengthBytesReceived =
      RecvTcpData(socketFd, &messageLength, sizeof(messageLength), 0);
  if (lengthBytesReceived != sizeof(messageLength)) {
    Log::getInstance().error(
        std::format("TCP -> Queue: Data Length Receive Failed."));
    return {};
  }

  // Convert message length from network byte order to host byte order
  messageLength = ntohl(messageLength);
  Log::getInstance().info(
      std::format("TCP -> Queue: Data Length {}", messageLength));

  // Prepare a buffer to receive the actual message
  std::vector<char> buffer(messageLength);

  size_t total_received = 0;
  while (total_received < messageLength) {
    int bytes = RecvTcpData(socketFd, buffer.data() + total_received,
                            messageLength - total_received, 0);
    if (bytes <= 0)
      break;
    total_received += bytes;
  }

  return buffer;
}
