#include "TcpConnection.h"
#include "Log.h"
#include "PerformanceCounter.h"
#include <chrono>
#include <mutex>

static int64_t NowSteadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void TcpConnection::diagMarkSendStart(uint64_t messageId)
{
    diagInFlightMessageIdVal.store(messageId);
    diagInFlightStartMsVal.store(NowSteadyMs());
    diagSendInFlight.store(true);
}

void TcpConnection::diagMarkSendEnd(uint64_t messageId)
{
    diagLastSendCompletedMessageIdVal.store(messageId);
    diagLastSendEndMsVal.store(NowSteadyMs());
    diagSendInFlight.store(false);
}

void TcpConnection::disconnect()
{
    std::function<void()> callbackToInvoke;
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
        // Capture callback to invoke outside the lock to prevent deadlock
        callbackToInvoke = disconnectCallback;
    }
    // Invoke callback after the lock is released
    if (callbackToInvoke) { callbackToInvoke(); }
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
            SocketLogLastError();
            disconnect();
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

