#pragma once

#include "Socket.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

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

    // Set callback for disconnect events
    void setDisconnectCallback(std::function<void()> callback);

    // Diagnostics: mark/inspect TCP send progress (used to debug VC reorder timeouts)
    void diagMarkSendStart(uint64_t messageId);
    void diagMarkSendEnd(uint64_t messageId);

    bool diagIsSendInFlight() const { return diagSendInFlight.load(); }
    uint64_t diagInFlightMessageId() const { return diagInFlightMessageIdVal.load(); }
    int64_t diagInFlightStartMs() const { return diagInFlightStartMsVal.load(); }
    uint64_t diagLastSendCompletedMessageId() const { return diagLastSendCompletedMessageIdVal.load(); }
    int64_t diagLastSendEndMs() const { return diagLastSendEndMsVal.load(); }

  private:
 public:
    std::function<void()> disconnectCallback;

    // Method to check if the connection is established
    bool isConnected() const { return connected.load(); };

    // Method to close the TCP connection
    void disconnect();

    // Get the socket file descriptor
    SocketFd getSocketFd() const;

  private:
    std::mutex disconnectMutex;         // Per-instance mutex for disconnect (not static — avoids cross-instance deadlock)
    std::atomic<bool> connected{false}; // Connection state (atomic for thread safety)
    SocketFd socketFd = -1;             // Socket file descriptor

    // Diagnostics-only state (atomic to avoid extra locking in send threads)
    std::atomic<bool> diagSendInFlight{false};
    std::atomic<uint64_t> diagInFlightMessageIdVal{0};
    std::atomic<int64_t> diagInFlightStartMsVal{0};
    std::atomic<uint64_t> diagLastSendCompletedMessageIdVal{0};
    std::atomic<int64_t> diagLastSendEndMsVal{0};
};

typedef std::shared_ptr<TcpConnection> TcpConnectionSp;
