#include "Socket.h"
#include "Log.h"
#include <format>
#include <chrono>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#include <cstring> // For strerror
#include <errno.h> // For errno
#include <sys/ioctl.h>
#endif

using namespace Logger;

// Socket creation and configuration functions
SocketFd CreateSocket(int domain, int type, int protocol) {
    SocketFd socketFd;
    
#ifdef _WIN32
    socketFd = socket(domain, type, protocol);
    if (socketFd == INVALID_SOCKET) {
        SocketLogLastError();
        return INVALID_SOCKET;
    }
#else
    socketFd = socket(domain, type, protocol);
    if (socketFd < 0) {
        SocketLogLastError();
        return -1;
    }
#endif

    return socketFd;
}

int SetSocketNonBlocking(SocketFd socketFd) {
#ifdef _WIN32
    // Set socket to non-blocking mode (Windows)
    u_long mode = 1;  // 1 = non-blocking
    return ioctlsocket(socketFd, FIONBIO, &mode);
#else
    // Set socket to non-blocking mode (POSIX)
    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags == -1) {
        SocketLogLastError();
        return -1;
    }
    return fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int SetSocketBlocking(SocketFd socketFd) {
#ifdef _WIN32
    // Set socket to blocking mode (Windows)
    u_long mode = 0;  // 0 = blocking
    return ioctlsocket(socketFd, FIONBIO, &mode);
#else
    // Set socket to blocking mode (POSIX)
    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags == -1) {
        SocketLogLastError();
        return -1;
    }
    return fcntl(socketFd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

bool IsSocketNonBlocking(SocketFd socketFd) {
#ifdef _WIN32
    // Check if socket is non-blocking (Windows)
    u_long mode = 0;
    if (ioctlsocket(socketFd, FIONBIO, &mode) != 0) {
        SocketLogLastError();
        return false;
    }
    return mode != 0;
#else
    // Check if socket is non-blocking (POSIX)
    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags == -1) {
        SocketLogLastError();
        return false;
    }
    return (flags & O_NONBLOCK) != 0;
#endif
}

// Socket polling functions
int SocketPoll(SocketFd socketFd, int events, int timeoutMs) {
#ifdef _WIN32
    // Windows implementation using select
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    
    if (events & POLLIN)
        FD_SET(socketFd, &readfds);
    if (events & POLLOUT)
        FD_SET(socketFd, &writefds);
    FD_SET(socketFd, &exceptfds);
    
    struct timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    
    int result = select(socketFd + 1, &readfds, &writefds, &exceptfds, &timeout);
    
    if (result > 0) {
        int revents = 0;
        if (FD_ISSET(socketFd, &readfds))
            revents |= POLLIN;
        if (FD_ISSET(socketFd, &writefds))
            revents |= POLLOUT;
        if (FD_ISSET(socketFd, &exceptfds))
            revents |= POLLERR;
        return revents;
    }
    return result; // 0 for timeout, -1 for error
#else
    // POSIX implementation using poll
    struct pollfd pfd;
    pfd.fd = socketFd;
    pfd.events = events;
    pfd.revents = 0;
    
    int result = poll(&pfd, 1, timeoutMs);
    if (result > 0) {
        return pfd.revents;
    }
    return result; // 0 for timeout, -1 for error
#endif
}

bool IsSocketReadable(SocketFd socketFd, int timeoutMs) {
    return (SocketPoll(socketFd, POLLIN, timeoutMs) & POLLIN) != 0;
}

bool IsSocketWritable(SocketFd socketFd, int timeoutMs) {
    return (SocketPoll(socketFd, POLLOUT, timeoutMs) & POLLOUT) != 0;
}

int SocketSelect(SocketFd socketFd, int timeoutSec) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socketFd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = timeoutSec;
    timeout.tv_usec = 0;

    return select(socketFd + 1, &readfds, NULL, NULL, &timeout);
}

// Standard blocking I/O operations
ssize_t SendTcpData(SocketFd socketFd, const void *data, size_t length, int flags) {
    log_info(std::format("SendTcpData: send {} bytes of data", length));
#if defined(_WIN32)
    return send(socketFd, (const char *)data, length, flags);
#else
    return send(socketFd, data, length, flags);
#endif
}

ssize_t SendUdpData(SocketFd socketFd, const void *data, size_t length,
                    int flags, const struct sockaddr *destAddr,
                    socklen_t destAddrLen) {
    log_info(std::format("SendUdpData: send {} bytes of data", length));
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
    ssize_t length = recvfrom(socketFd, (char *)buffer, bufferSize, flags, srcAddr, srcAddrLen);
    if (length == -1) {
        int lastError = WSAGetLastError();
        log_error(std::format("RecvUdpData: log_error receiving data, log_error code: {}", lastError));
    }
#else
    ssize_t length = recvfrom(socketFd, buffer, bufferSize, flags, srcAddr, srcAddrLen);
#endif
    log_info(std::format("RecvUdpData: receive {} bytes of data", length));
    return length;
}

ssize_t RecvTcpData(SocketFd socketFd, void *buffer, size_t bufferSize, int flags) {
#if defined(_WIN32)
    ssize_t length = recv(socketFd, (char *)buffer, bufferSize, flags);
#else
    ssize_t length = recv(socketFd, buffer, bufferSize, flags);
#endif 
    log_info(std::format("RecvTcpData: receive {} bytes of data", length));
    return length;
}

// Non-blocking I/O operations with timeout
ssize_t SendTcpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags, int timeoutMs) {

    log_info(std::format("Try to send {} bytes data", length));

    // Check if socket is writable within timeout
    if (!IsSocketWritable(socketFd, timeoutMs)) {
        return SOCKET_ERROR_TIMEOUT;
    }
    
    
    // Try to send data
#if defined(_WIN32)
    ssize_t bytesSent = send(socketFd, (const char *)data, length, flags);
#else
    ssize_t bytesSent = send(socketFd, data, length, flags);
#endif

    // Check for errors
    if (bytesSent < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        log_info(std::format("error: {}", error));
        if (error == WSAEWOULDBLOCK) {
            bytesSent = SOCKET_ERROR_WOULD_BLOCK;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            bytesSent = SOCKET_ERROR_WOULD_BLOCK;
        } else if (errno == EINTR) {
            bytesSent = SOCKET_ERROR_INTERRUPTED;
        }
#endif
    }
    
    
    if (bytesSent > 0) {
        log_info(std::format("SendTcpDataNonBlocking: sent {} bytes of data", bytesSent));
    }
    
    return bytesSent;
}

ssize_t SendUdpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags,
                              const struct sockaddr *destAddr, socklen_t destAddrLen, int timeoutMs) {
    // Check if socket is writable within timeout
    if (!IsSocketWritable(socketFd, timeoutMs)) {
        return SOCKET_ERROR_TIMEOUT;
    }
    
    // Try to send data
#if defined(_WIN32)
    ssize_t bytesSent = sendto(socketFd, (const char *)data, length, flags, destAddr, destAddrLen);
#else
    ssize_t bytesSent = sendto(socketFd, data, length, flags, destAddr, destAddrLen);
#endif

    // Check for errors
    if (bytesSent < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            bytesSent = SOCKET_ERROR_WOULD_BLOCK;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            bytesSent = SOCKET_ERROR_WOULD_BLOCK;
        } else if (errno == EINTR) {
            bytesSent = SOCKET_ERROR_INTERRUPTED;
        }
#endif
    }
    
    
    if (bytesSent > 0) {
        log_info(std::format("SendUdpDataNonBlocking: sent {} bytes of data", bytesSent));
    }
    
    return bytesSent;
}

ssize_t RecvTcpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int timeoutMs) {
    // Check if socket is readable within timeout
    if (!IsSocketReadable(socketFd, timeoutMs)) {
        return SOCKET_ERROR_TIMEOUT;
    }
    
    // Try to receive data
#if defined(_WIN32)
    ssize_t bytesReceived = recv(socketFd, (char *)buffer, bufferSize, flags);
#else
    ssize_t bytesReceived = recv(socketFd, buffer, bufferSize, flags);
#endif

    // Check for errors
    if (bytesReceived < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            bytesReceived = SOCKET_ERROR_WOULD_BLOCK;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            bytesReceived = SOCKET_ERROR_WOULD_BLOCK;
        } else if (errno == EINTR) {
            bytesReceived = SOCKET_ERROR_INTERRUPTED;
        }
#endif
    } else if (bytesReceived == 0) {
        // Connection closed
        bytesReceived = SOCKET_ERROR_CLOSED;
    }
    
    if (bytesReceived > 0) {
        log_info(std::format("RecvTcpDataNonBlocking: received {} bytes of data", bytesReceived));
    }
    
    return bytesReceived;
}

ssize_t RecvUdpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags,
                              struct sockaddr *srcAddr, socklen_t *srcAddrLen, int timeoutMs) {
    // Check if socket is readable within timeout
    if (!IsSocketReadable(socketFd, timeoutMs)) {
        return SOCKET_ERROR_TIMEOUT;
    }
    
    
    // Try to receive data
#if defined(_WIN32)
    ssize_t bytesReceived = recvfrom(socketFd, (char *)buffer, bufferSize, flags, srcAddr, srcAddrLen);
#else
    ssize_t bytesReceived = recvfrom(socketFd, buffer, bufferSize, flags, srcAddr, srcAddrLen);
#endif

    // Check for errors
    if (bytesReceived < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            bytesReceived = SOCKET_ERROR_WOULD_BLOCK;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            bytesReceived = SOCKET_ERROR_WOULD_BLOCK;
        } else if (errno == EINTR) {
            bytesReceived = SOCKET_ERROR_INTERRUPTED;
        }
#endif
    }
    
    if (bytesReceived > 0) {
        log_info(std::format("RecvUdpDataNonBlocking: received {} bytes of data", bytesReceived));
    }
    
    return bytesReceived;
}

// Socket control operations
int SocketListen(SocketFd socketFd, int backlog) {
    return listen(socketFd, backlog);
}

int SocketConnect(SocketFd socketFd, const struct sockaddr *destAddr, socklen_t destAddrLen) {
    auto result = connect(socketFd, destAddr, destAddrLen);
    if (result < 0) {
        SocketLogLastError();
    }
    return result;
}

int SocketConnectNonBlocking(SocketFd socketFd, const struct sockaddr *destAddr, socklen_t destAddrLen, int timeoutMs) {
    
    // Attempt to connect
    int result = connect(socketFd, destAddr, destAddrLen);
    
    if (result < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) {
            // Connection in progress, wait for it to complete
            if (IsSocketWritable(socketFd, timeoutMs)) {
                // Check if connection succeeded
                int optval;
                socklen_t optlen = sizeof(optval);
                if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, (char*)&optval, &optlen) == 0) {
                    if (optval == 0) {
                        result = 0; // Connection succeeded
                    } else {
                        WSASetLastError(optval);
                        result = -1;
                    }
                }
            } else {
                // Timeout
                WSASetLastError(WSAETIMEDOUT);
                result = -1;
            }
        }
#else
        if (errno == EINPROGRESS) {
            // Connection in progress, wait for it to complete
            if (IsSocketWritable(socketFd, timeoutMs)) {
                // Check if connection succeeded
                int optval;
                socklen_t optlen = sizeof(optval);
                if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == 0) {
                    if (optval == 0) {
                        result = 0; // Connection succeeded
                    } else {
                        errno = optval;
                        result = -1;
                    }
                }
            } else {
                // Timeout
                errno = ETIMEDOUT;
                result = -1;
            }
        }
#endif
    }
    
    
    return result;
}

int SocketClose(SocketFd socketFd) {
    log_info("close the socket");
#ifdef _WIN32
    return closesocket(socketFd);
#else
    return close(socketFd);
#endif
}

// Specialized receive functions
ssize_t RecvTcpDataWithSize(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int bytesToRead) {
    char *bufPtr = static_cast<char *>(buffer);
    ssize_t totalBytesRead = 0;

    while (totalBytesRead < bytesToRead) {
        ssize_t bytesRead = recv(socketFd, bufPtr + totalBytesRead, bytesToRead - totalBytesRead, flags);

        if (bytesRead < 0) {
            // An error occurred, log the error and return
            SocketLogLastError();
            return -1;
        } else if (bytesRead == 0) {
            // The connection was closed by the peer
            log_info("RecvTcpDataWithSize: connection closed by peer");
            return totalBytesRead;
        }

        totalBytesRead += bytesRead;
    }

    log_info(std::format("RecvTcpDataWithSize: successfully received {} bytes of data", totalBytesRead));
    return totalBytesRead;
}

ssize_t RecvTcpDataWithSizeNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int bytesToRead, int timeoutMs) {
    char *bufPtr = static_cast<char *>(buffer);
    ssize_t totalBytesRead = 0;
    
    
    // Calculate deadline
    auto startTime = std::chrono::steady_clock::now();
    auto deadline = startTime + std::chrono::milliseconds(timeoutMs);
    
    while (totalBytesRead < bytesToRead) {
        // Calculate remaining timeout
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return SOCKET_ERROR_TIMEOUT;
        }
        
        int remainingTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        
        // Check if socket is readable
        if (!IsSocketReadable(socketFd, remainingTimeoutMs)) {
            return SOCKET_ERROR_TIMEOUT;
        }
        
        // Try to read data
#if defined(_WIN32)
        ssize_t bytesRead = recv(socketFd, bufPtr + totalBytesRead, bytesToRead - totalBytesRead, flags);
#else
        ssize_t bytesRead = recv(socketFd, bufPtr + totalBytesRead, bytesToRead - totalBytesRead, flags);
#endif

        if (bytesRead < 0) {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // No data available, try again
                std::this_thread::yield();
                continue;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, try again
                std::this_thread::yield();
                continue;
            } else if (errno == EINTR) {
                // Interrupted, try again
                continue;
            }
#endif
            // Other error
            SocketLogLastError();
            return -1;
        } else if (bytesRead == 0) {
            // Connection closed
            log_info("RecvTcpDataWithSizeNonBlocking: connection closed by peer");
            return totalBytesRead;
        }
        
        totalBytesRead += bytesRead;
    }
    
    log_info(std::format("RecvTcpDataWithSizeNonBlocking: successfully received {} bytes of data", totalBytesRead));
    return totalBytesRead;
}

// Error handling
void SocketLogLastError() {
#ifdef _WIN32
    int lastError = WSAGetLastError();
    log_error(std::format("Socket log_error: {}", lastError));
#else
    int lastError = errno;
    log_error(std::format("Socket error: {} - {}", lastError, strerror(lastError)));
#endif
}

int SocketBind(SocketFd socketFd, const struct sockaddr *addr, socklen_t addrLen)
{
    auto r = bind(socketFd, addr, addrLen);
    if (r != 0) {
        SocketLogLastError();
    }
    return r;
}

SocketFd SocketAccept(SocketFd socketFd, struct sockaddr *addr, socklen_t *addrLen)
{
    return accept(socketFd, addr, addrLen);
}

void InitializeSockets()
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        // Handle error
        log_error(std::string("WSAStartup failed: %d\n", result));
        exit(EXIT_FAILURE);
    }
#endif
}

void CleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

