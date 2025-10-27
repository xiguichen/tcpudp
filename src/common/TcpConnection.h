#pragma once

#include "Socket.h"
#include <cstddef>
#include <functional>
#include <memory>

class TcpConnection
{

  public:
    TcpConnection(SocketFd fd) : socketFd(fd), connected(true)
    {
        // set the send timeout to 1.8 seconds
        int timeoutMs = 1800;
#ifdef _WIN32
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
#else
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
#endif
    };

    ~TcpConnection() = default;

    // Method to send data over the TCP connection
    void send(const char *data, size_t size);

    // Method to receive data from the TCP connection
    size_t receive(char *buffer, size_t bufferSize);

    // Method to check if the connection is established
    bool isConnected() const;

    // Method to close the TCP connection
    void disconnect();

    // Get the socket file descriptor
    SocketFd getSocketFd() const;

    // Set callback for send timeout handling
    void setOnSendTimeoutCallback(const std::function<void(char *buffer, size_t size)> &callback);

  private:
    bool connected = false;                                                 // Connection state
    SocketFd socketFd = -1;                                                 // Socket file descriptor
    std::function<void(char *buffer, size_t size)> onSendTimeout = nullptr; // Callback for handle send timeout
};

typedef std::shared_ptr<TcpConnection> TcpConnectionSp;
