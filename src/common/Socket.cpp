#include "Socket.h"
#include "Log.h"
#include <format>
#ifndef _WIN32
#include <unistd.h>
#include <cstring> // For strerror
#include <errno.h> // For errno
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
    ssize_t length = recvfrom(socketFd, (char *)buffer, bufferSize, flags, srcAddr, srcAddrLen);
    if (length == -1) {
        int lastError = WSAGetLastError();
        Log::getInstance().error(std::format("RecvUdpData: error receiving data, error code: {}", lastError));
    }
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

int SocketSelect(SocketFd socketFd, int timeoutSec) {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(socketFd, &readfds);

  struct timeval timeout;
  timeout.tv_sec = timeoutSec;
  timeout.tv_usec = 0;

  return select(socketFd + 1, &readfds, NULL, NULL, &timeout);
}

void SocketLogLastError()
{

#ifdef _WIN32
  int lastError = WSAGetLastError();
  Log::getInstance().error(std::format("Socket error: {}", lastError));
#else
  // Linux/macOS error handling for log errorCode
  //
  int lastError = errno;
  Log::getInstance().error(std::format("Socket error: {} - {}", lastError, strerror(lastError)));

#endif
}

ssize_t RecvTcpDataWithSize(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int bytesToRead)
{
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
            Log::getInstance().info("RecvTcpDataWithSize: connection closed by peer");
            return totalBytesRead;
        }

        totalBytesRead += bytesRead;
    }

    Log::getInstance().info(std::format("RecvTcpDataWithSize: successfully received {} bytes of data", totalBytesRead));
    return totalBytesRead;
}
