#include "Socket.h"
#include "Log.h"
#include <format>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace Logger;

ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length,
                    int flags) {

  Log::getInstance().info(std::format("SendTcpData: send {} bytes of data", length));
#if defined(_WIN32)
  return send(socketFd, (const char *)data, length, flags);
#else
  return send(socketFd, data, length, flags);
#endif
}
ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length,
                    int flags, const struct sockaddr *destAddr,
                    socklen_t destAddrLen) {
  Log::getInstance().info(std::format("SendUdpData: send {} bytes of data", length));
#if defined(_WIN32)
  return sendto(socketFd, (const char *)data, length, flags, destAddr, destAddrLen);
#else
  return sendto(socketFd, data, length, flags, destAddr, destAddrLen);
#endif
}

ssize_t RecvUdpData(SocketFd socketFd, void *buffer, size_t bufferSize,
                    int flags, struct sockaddr *srcAddr,
                    socklen_t *srcAddrLen) {

#if defined(_WIN32)
    ssize_t length =  recvfrom(socketFd, (char *)buffer, bufferSize, flags, srcAddr, srcAddrLen);
#else
  ssize_t length =  recvfrom(socketFd, buffer, bufferSize, flags, srcAddr, srcAddrLen);
#endif
  Log::getInstance().info(std::format("RecvUdpData: receive {} bytes of data", length));
  return length;
}

ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize,
                    int flags) {
#if defined(_WIN32)
    ssize_t length =  recv(socketFd, (char *)buffer, bufferSize, flags);
#else
  ssize_t length =  recv(socketFd, buffer, bufferSize, flags);
#endif 
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
int SocketClose(SocketFd socketFd) {
#ifdef _WIN32
  return closesocket(socketFd);
#else
  return close(socketFd);
#endif
}

