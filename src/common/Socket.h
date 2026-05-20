#pragma once

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int ssize_t;     // Define ssize_t for Windows
typedef SOCKET SocketFd; // Use SOCKET type for Windows
typedef SOCKADDR_IN sockaddr_in;
typedef SOCKADDR sockaddr;
// typedef int socklen_t;
#pragma comment(lib, "ws2_32.lib")

#define SHUT_RDWR SD_BOTH

// MIB_TCP_INFO structure for Windows
#ifndef MIB_TCP_STATE_CLOSED
#define MIB_TCP_STATE_CLOSED 1
#define MIB_TCP_STATE_LISTEN 2
#define MIB_TCP_STATE_SYN_SENT 3
#define MIB_TCP_STATE_SYN_RECEIVED 4
#define MIB_TCP_STATE_ESTABLISHED 5
#define MIB_TCP_STATE_FIN_WAIT1 6
#define MIB_TCP_STATE_FIN_WAIT2 7
#define MIB_TCP_STATE_CLOSE_WAIT 8
#define MIB_TCP_STATE_CLOSING 9
#define MIB_TCP_STATE_LAST_ACK 10
#define MIB_TCP_STATE_TIME_WAIT 11
#define MIB_TCP_STATE_DELETE_TCB 12
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
typedef int SocketFd; // Use int type for Linux/macOS
#endif

// Socket creation and configuration
SocketFd SocketCreate(int domain, int type, int protocol);
int SetSocketNonBlocking(SocketFd socketFd);
int SetSocketBlocking(SocketFd socketFd);
bool IsSocketNonBlocking(SocketFd socketFd);

// Socket I/O operations
ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length, int flags);
ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length, int flags, const struct sockaddr *destAddr,
                    socklen_t destAddrLen);
ssize_t RecvUdpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, struct sockaddr *srcAddr,
                    socklen_t *srcAddrLen);
ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags);

// Non-blocking I/O operations with timeout
ssize_t SendTcpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags, int timeoutMs);
ssize_t SendUdpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags,
                               const struct sockaddr *destAddr, socklen_t destAddrLen, int timeoutMs);
ssize_t RecvUdpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, struct sockaddr *srcAddr,
                               socklen_t *srcAddrLen, int timeoutMs);
ssize_t RecvTcpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int timeoutMs);

// Specialized receive functions
ssize_t RecvTcpDataWithSize(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int bytesToRead);
ssize_t RecvTcpDataWithSizeNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int bytesToRead,
                                       int timeoutMs);

// Socket control operations
int SocketListen(SocketFd socketFd, int backlog);
int SocketConnect(SocketFd socketFd, const struct sockaddr *destAddr, socklen_t destAddrLen);
int SocketConnectNonBlocking(SocketFd socketFd, const struct sockaddr *destAddr, socklen_t destAddrLen, int timeoutMs);
int SocketClose(SocketFd socketFd);
int SocketShutdown(SocketFd socketFd, int how);
int SocketBind(SocketFd socketFd, const struct sockaddr *addr, socklen_t addrLen);

SocketFd SocketAccept(SocketFd socketFd, struct sockaddr *addr, socklen_t *addrLen);

// Socket polling
int SocketSelect(SocketFd socketFd, int timeoutSec);
int SocketPoll(SocketFd socketFd, int events, int timeoutMs);
int SocketPollMany(struct pollfd *fds, size_t count, int timeoutMs);
bool IsSocketReadable(SocketFd socketFd, int timeoutMs);
bool IsSocketWritable(SocketFd socketFd, int timeoutMs);
int SocketBytesAvailable(SocketFd socketFd);

// Direct non-blocking I/O for sockets already set non-blocking.
// These skip the redundant poll() that SendTcpDataNonBlocking / RecvTcpDataNonBlocking perform.
ssize_t SendTcpDirect(SocketFd socketFd, const void *data, size_t length, int flags);
ssize_t RecvTcpDirect(SocketFd socketFd, void *buffer, size_t bufferSize, int flags);

// Error handling
int SocketLogLastError();

// Socket error codes
enum SocketError
{
    SOCKET_ERROR_NONE = 0,
    SOCKET_ERROR_WOULD_BLOCK = -1,
    SOCKET_ERROR_TIMEOUT = -2,
    SOCKET_ERROR_CLOSED = -3,
    SOCKET_ERROR_INTERRUPTED = -4,
    SOCKET_ERROR_OTHER = -5
};

void SocketInit();

void SocketClearnup();

int SocketSetTcpNoDelay(SocketFd socketFd, bool noDelay);

int SocketSetSendBufferSize(SocketFd socketFd, int size);

int SocketSetReceiveBufferSize(SocketFd socketFd, int size);

void SocketReuseAddress(SocketFd socketFd);
