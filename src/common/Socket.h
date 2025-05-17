#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int ssize_t;  // Define ssize_t for Windows
typedef SOCKET SocketFd;  // Use SOCKET type for Windows
typedef SOCKADDR_IN sockaddr_in;
typedef SOCKADDR sockaddr;
// typedef int socklen_t;
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <poll.h>
typedef int SocketFd;  // Use int type for Linux/macOS
#endif

// Socket creation and configuration
SocketFd CreateSocket(int domain, int type, int protocol);
int SetSocketNonBlocking(SocketFd socketFd);
int SetSocketBlocking(SocketFd socketFd);
bool IsSocketNonBlocking(SocketFd socketFd);

// Socket I/O operations
ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length, int flags);
ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length, int flags,
                    const struct sockaddr *destAddr, socklen_t destAddrLen);
ssize_t RecvUdpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags,
                    struct sockaddr *srcAddr, socklen_t *srcAddrLen);
ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags);

// Non-blocking I/O operations with timeout
ssize_t SendTcpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags, int timeoutMs);
ssize_t SendUdpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags,
                              const struct sockaddr *destAddr, socklen_t destAddrLen, int timeoutMs);
ssize_t RecvUdpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags,
                              struct sockaddr *srcAddr, socklen_t *srcAddrLen, int timeoutMs);
ssize_t RecvTcpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int timeoutMs);

// Specialized receive functions
ssize_t RecvTcpDataWithSize(SocketFd socketFd, void *buffer, size_t bufferSize,
                            int flags, int bytesToRead);
ssize_t RecvTcpDataWithSizeNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize,
                                      int flags, int bytesToRead, int timeoutMs);

// Socket control operations
int SocketListen(SocketFd socketFd, int backlog);
int SocketConnect(SocketFd socketFd, const struct sockaddr *destAddr, socklen_t destAddrLen);
int SocketConnectNonBlocking(SocketFd socketFd, const struct sockaddr *destAddr, socklen_t destAddrLen, int timeoutMs);
int SocketClose(SocketFd socketFd);

// Socket polling
int SocketSelect(SocketFd socketFd, int timeoutSec);
int SocketPoll(SocketFd socketFd, int events, int timeoutMs);
bool IsSocketReadable(SocketFd socketFd, int timeoutMs);
bool IsSocketWritable(SocketFd socketFd, int timeoutMs);

// Error handling
void SocketLogLastError();

// Socket error codes
enum SocketError {
    SOCKET_ERROR_NONE = 0,
    SOCKET_ERROR_WOULD_BLOCK = -1,
    SOCKET_ERROR_TIMEOUT = -2,
    SOCKET_ERROR_CLOSED = -3,
    SOCKET_ERROR_INTERRUPTED = -4,
    SOCKET_ERROR_OTHER = -5
};
