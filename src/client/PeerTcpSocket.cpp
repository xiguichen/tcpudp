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
  std::vector<char> newData;
  UvtUtils::AppendUdpData(data, ++sendId, newData);
  SendTcpData(socketFd, newData.data(), newData.size(), 0);
}

std::vector<char> PeerTcpSocket::receive() {

  UvtHeader header;
  ssize_t lengthBytesReceived =
      RecvTcpDataWithSize(socketFd, &header, HEADER_SIZE, 0, HEADER_SIZE);
  if (lengthBytesReceived != HEADER_SIZE) {
    Log::getInstance().error(
        std::format("TCP -> Queue: Data Header Receive Failed."));
    return {};
  }

  // Convert message length from network byte order to host byte order
  uint16_t messageLength = ntohs(header.size);
  Log::getInstance().info(std::format(
      "TCP -> Queue: Data ID {} , Data Length: {}, Data Checksum: {}",
      header.id, messageLength, header.checksum));

  // Prepare a buffer to receive the actual message
  std::vector<char> buffer(messageLength);

  uint16_t total_received = 0;
  while (total_received < messageLength) {
    int bytes = RecvTcpData(socketFd, buffer.data() + total_received,
                            messageLength - total_received, 0);
    if (bytes <= 0)
      break;
    total_received += bytes;
  }
  uint8_t checksum = xor_checksum((uint8_t *)buffer.data(), messageLength);
  if (checksum != header.checksum) {
    Log::getInstance().error("Tcp -> Queue: Checksum verify failed");
    return {};
  }

  return buffer;
}
PeerTcpSocket::~PeerTcpSocket() { SocketClose(socketFd); }

void PeerTcpSocket::sendHandshake() { SendTcpData(socketFd, "0.0.1", 5, 0); }
