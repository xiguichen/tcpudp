#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int ssize_t;  // Define ssize_t for Windows
typedef SOCKET SocketFd;  // Use SOCKET type for Windows
typedef SOCKADDR_IN sockaddr_in;
typedef SOCKADDR sockaddr;
typedef int socklen_t;
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
typedef int SocketFd;  // Use int type for Linux/macOS
#endif

ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length, int flags);
ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length, int flags,
                    const struct sockaddr *destAddr, socklen_t destAddrLen);
ssize_t RecvUdpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags,
                    struct sockaddr *srcAddr, socklen_t *srcAddrLen);
ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags);

int SocketListen(SocketFd socketFd, int backlog);

// Socket Connect
int SocketConnect(SocketFd socketFd, const struct sockaddr *destAddr,
                  socklen_t destAddrLen);

// Socket Close
int SocketClose(SocketFd socketFd);
