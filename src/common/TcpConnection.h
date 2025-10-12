#pragma once

#include <cstddef>
#include <memory>
#include "Socket.h"

class TcpConnection
{

    public:
        TcpConnection(SocketFd fd) : socketFd(fd), connected(true) {}
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

      private:
        bool connected = false; // Connection state
        SocketFd socketFd = -1; // Socket file descriptor

};

typedef std::shared_ptr<TcpConnection> TcpConnectionSp;
