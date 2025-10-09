#include "TcpConnection.h"

void TcpConnection::disconnect()
{
    if (socketFd != -1)
    {
        SocketClose(socketFd);
        socketFd = -1;
    }
    connected = false;
    log_info("TCP connection closed");
}
bool TcpConnection::isConnected() const
{
    return connected;
}

size_t TcpConnection::receive(char *buffer, size_t bufferSize)
{
    if (connected && socketFd != -1)
    {
        auto bytesReceived = RecvTcpData(socketFd, buffer, bufferSize, 0);
        if (bytesReceived == -1)
        {
            // Handle receive error
            log_error("Failed to receive data from TCP connection");
            connected = false;
            return 0;
        }
        return static_cast<size_t>(bytesReceived);
    }
    else
    {
        // Handle not connected state
        log_error("Attempted to receive data on a disconnected TCP connection");
        return 0;
    }
}

void TcpConnection::send(const char *data, size_t size)
{
    if (connected && socketFd != -1)
    {
        auto bytesSent = SendTcpData(socketFd, data, size, 0);
        if (bytesSent == -1)
        {
            // Handle send error
            log_error("Failed to send data over TCP connection");
            connected = false;
        }
    }
    else
    {
        // Handle not connected state
        log_error("Attempted to send data on a disconnected TCP connection");
    }
}

