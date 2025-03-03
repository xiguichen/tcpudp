#pragma once

#include <sys/socket.h>

typedef int SocketFd;

ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length, int flags);
ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length, int flags,
                    const struct sockaddr *destAddr, socklen_t destAddrLen);
ssize_t RecvUdpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags,
                    struct sockaddr *srcAddr, socklen_t *srcAddrLen);
ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags);

int SocketListen(SocketFd socketFd, int backlog);
