#include "PeerTcpSocket.h"
#include <Log.h>
#include <Socket.h>
#include <arpa/inet.h>
#include <format>
#include <unistd.h>
#include <vector>
#include <Protocol.h>
#include <cstring>

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

  DataHeader header;
  header.size = htons(data.size());
  header.id = sendId++;
  header.checksum = xor_checksum((uint8_t*)data.data(), data.size());
  uint32_t dataLength = htonl(data.size());
  Log::getInstance().info(
      std::format("Queue -> TCP: Data ID {} , Data Length: {}, Data Checksum: {}", header.id, data.size(), header.checksum));
  std::vector<char> newData(HEADER_SIZE + data.size());
  int newLength = HEADER_SIZE + data.size();
  memcpy(newData.data(), &header, HEADER_SIZE);
  memcpy(newData.data() + HEADER_SIZE, data.data(), data.size());
  SendTcpData(socketFd, newData.data(), newLength, 0);
}

std::vector<char> PeerTcpSocket::receive() {

  DataHeader header;
  ssize_t lengthBytesReceived =
      RecvTcpData(socketFd, &header, HEADER_SIZE, 0);
  if (lengthBytesReceived != HEADER_SIZE) {
    Log::getInstance().error(
        std::format("TCP -> Queue: Data Header Receive Failed."));
    return {};
  }

  // Convert message length from network byte order to host byte order
  uint16_t messageLength = ntohs(header.size);
  Log::getInstance().info(
      std::format("TCP -> Queue: Data ID {} , Data Length: {}, Data Checksum: {}", header.id, messageLength, header.checksum));

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
  uint8_t checksum = xor_checksum((uint8_t*)buffer.data(), messageLength);
  if(checksum != header.checksum)
  {
    Log::getInstance().error("Tcp -> Queue: Checksum verify failed");
    return {};
  }
  

  return buffer;
}
