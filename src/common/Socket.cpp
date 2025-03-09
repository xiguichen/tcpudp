#include "Socket.h"
#include "Log.h"
#include <format>

using namespace Logger;

ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length,
                    int flags) {

  Log::getInstance().info(std::format("SendTcpData: send {} bytes of data", length));
  return send(socketFd, data, length, flags);
}
ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length,
                    int flags, const struct sockaddr *destAddr,
                    socklen_t destAddrLen) {
  Log::getInstance().info(std::format("SendUdpData: send {} bytes of data", length));
  return sendto(socketFd, data, length, flags, destAddr, destAddrLen);
}

ssize_t RecvUdpData(SocketFd socketFd, void *buffer, size_t bufferSize,
                    int flags, struct sockaddr *srcAddr,
                    socklen_t *srcAddrLen) {

  ssize_t length =  recvfrom(socketFd, buffer, bufferSize, flags, srcAddr, srcAddrLen);
  Log::getInstance().info(std::format("RecvUdpData: receive {} bytes of data", length));
  return length;
}

ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize,
                    int flags) {
  ssize_t length =  recv(socketFd, buffer, bufferSize, flags);
  Log::getInstance().info(std::format("RecvTcpData: receive {} bytes of data", length));
  return length;
}

int SocketListen(SocketFd socketFd, int backlog) {
  return listen(socketFd, backlog);
}

int SocketConnect(SocketFd socketFd, const struct sockaddr *destAddr,
                  socklen_t destAddrLen) {
  return connect(socketFd, destAddr, destAddrLen);
}
