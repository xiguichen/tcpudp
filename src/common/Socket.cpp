#include "Socket.h"
#include <iostream>

ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length,
                    int flags) {

  std::cout << __func__ << ": send " << length << " bytes of data" << std::endl;
  return send(socketFd, data, length, flags);
}
ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length,
                    int flags, const struct sockaddr *destAddr,
                    socklen_t destAddrLen) {
  return sendto(socketFd, data, length, flags, destAddr, destAddrLen);
}

ssize_t RecvUdpData(SocketFd socketFd, void *buffer, size_t bufferSize,
                    int flags, struct sockaddr *srcAddr,
                    socklen_t *srcAddrLen) {
  ssize_t size = recvfrom(socketFd, buffer, bufferSize, flags, srcAddr, srcAddrLen);
  std::cout << "RecvUdpData:" << *srcAddrLen << std::endl;
  return size;
}

ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize,
                    int flags) {
  return recv(socketFd, buffer, bufferSize, flags);
}

int SocketListen(SocketFd socketFd, int backlog) {
  return listen(socketFd, backlog);
}

int SocketConnect(SocketFd socketFd, const struct sockaddr *destAddr,
                  socklen_t destAddrLen) {
  return connect(socketFd, destAddr, destAddrLen);
}
