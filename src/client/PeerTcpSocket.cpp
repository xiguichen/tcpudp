#include "PeerTcpSocket.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <iostream>

PeerTcpSocket::PeerTcpSocket(const std::string &address, int port) {
  socketFd = socket(AF_INET, SOCK_STREAM, 0);
  connect(address, port);
}

void PeerTcpSocket::connect(const std::string &address, int port) {
  peerAddress.sin_family = AF_INET;
  peerAddress.sin_port = htons(port);
  inet_pton(AF_INET, address.c_str(), &peerAddress.sin_addr);
  ::connect(socketFd, (struct sockaddr *)&peerAddress, sizeof(peerAddress));
}

void PeerTcpSocket::send(const std::vector<char> &data) {
  // send the data length first
  uint32_t dataLength = htonl(data.size());
  ::send(socketFd, &dataLength, sizeof(dataLength), 0);
  ::send(socketFd, data.data(), data.size(), 0);
}

std::vector<char> PeerTcpSocket::receive() {
  // First, read the length of the message
  uint32_t messageLength = 0;
  ssize_t lengthBytesReceived =
      ::recv(socketFd, &messageLength, sizeof(messageLength), 0);
  if (lengthBytesReceived != sizeof(messageLength)) {
    std::cerr << "Error reading message length from socket" << std::endl;
    return {};
  }

  // Convert message length from network byte order to host byte order
  messageLength = ntohl(messageLength);
  std::cout << "Received message length: " << messageLength << std::endl;

  // Prepare a buffer to receive the actual message
  std::vector<char> buffer(messageLength);
  ssize_t bytesReceived = ::recv(socketFd, buffer.data(), messageLength, 0);
  if (bytesReceived != messageLength) {
    std::cerr << "Error reading full message from socket" << std::endl;
    return {};
  }

  return buffer;
}
