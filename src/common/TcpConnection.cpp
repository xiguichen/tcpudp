#include "TcpConnection.h"
#include "Log.h"
#include "PerformanceCounter.h"
#include <mutex>

// Mutex to protect disconnect operations
static std::mutex disconnectMutex;

void TcpConnection::disconnect()
{
    std::lock_guard<std::mutex> lock(disconnectMutex);
    
    // Check if already disconnected
    if (!connected.load()) {
        return;
    }
    
    if (socketFd != -1)
    {
        SocketClose(socketFd);
        socketFd = -1;
    }
    connected.store(false);
    log_debug("Resetting BlockingQueue post-disconnect.");
    if (disconnectCallback) { disconnectCallback(); } // Notify virtual channels
    log_info("TCP connection closed");
}

size_t TcpConnection::receive(char *buffer, size_t bufferSize)
{
    if (connected.load() && socketFd != -1)
    {
        auto bytesReceived = RecvTcpData(socketFd, buffer, bufferSize, 0);
        if (bytesReceived == -1)
        {
            // Handle receive error
            log_error("Failed to receive data from TCP connection");

            SocketLogLastError();

            // Close this connection
            disconnect();
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
    if (connected.load() && socketFd != -1)
    {
        auto bytesSent = SendTcpData(socketFd, data, size, 0);
        if (bytesSent == -1)
        {
            // Handle send error
            log_error("Failed to send data over TCP connection");
            int lastError = SocketLogLastError();
#ifdef _WIN32
            if (lastError == WSAETIMEDOUT)
#else 
            if (lastError == ETIMEDOUT)
#endif
            {
                // Invoke the send timeout callback if set
                if (onSendTimeout)
                {
                    onSendTimeout(const_cast<char *>(data), size);
                }
            }
            else {
                disconnect();
            }
        }
    }
    else
    {
        // Handle not connected state
        log_error("Attempted to send data on a disconnected TCP connection");
    }
}

SocketFd TcpConnection::getSocketFd() const
{
    return socketFd;
}

void TcpConnection::setOnSendTimeoutCallback(const std::function<void(char *buffer, size_t size)> &callback)
{
    onSendTimeout = callback;
}

