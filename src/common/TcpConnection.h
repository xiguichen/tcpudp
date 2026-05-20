#pragma once

#include "Socket.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

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

    bool diagIsSendInFlight() const
    {
        return diagSendInFlight.load();
    }
    uint64_t diagInFlightMessageId() const
    {
        return diagInFlightMessageIdVal.load();
    }
    int64_t diagInFlightStartMs() const
    {
        return diagInFlightStartMsVal.load();
    }
    uint64_t diagLastSendCompletedMessageId() const
    {
        return diagLastSendCompletedMessageIdVal.load();
    }
    int64_t diagLastSendEndMs() const
    {
        return diagLastSendEndMsVal.load();
    }

    // Socket quality-aware sending: runtime info and congestion monitoring
    struct TcpConnectionRuntimeInfo
    {
        bool supported = false;
        bool valid = false;
        bool isInExponentialBackoff = false;
        bool retransmissionIndicatorIsBytes = false;
        uint32_t rtoUs = 0;
        uint32_t smoothedRttUs = 0;
        uint32_t congestionWindowBytes = 0;
        uint32_t bytesInFlight = 0;
        uint32_t retransmissionIndicator = 0;
        uint32_t timeoutEpisodes = 0;
    };

    TcpConnectionRuntimeInfo sampleRuntimeInfo();
    bool refreshRuntimeInfoIfStale(std::chrono::milliseconds refreshInterval);
    /// Lock-free read of the last cached runtime info. Safe to call from any thread.
    TcpConnectionRuntimeInfo getLastRuntimeInfo() const
    {
        auto gen = runtimeInfoGeneration.load(std::memory_order_acquire);
        TcpConnectionRuntimeInfo copy;
        do
        {
            gen = runtimeInfoGeneration.load(std::memory_order_acquire);
            copy = lastRuntimeInfo; // shallow copy of POD
        } while (gen != runtimeInfoGeneration.load(std::memory_order_acquire));
        return copy;
    }

  private:
  public:
    std::function<void()> disconnectCallback;

    // Method to check if the connection is established
    bool isConnected() const
    {
        return connected.load();
    };

    // Method to close the TCP connection
    void disconnect();

    // Get the socket file descriptor
    SocketFd getSocketFd() const;

  private:
    std::mutex disconnectMutex; // Per-instance mutex for disconnect (not static — avoids cross-instance deadlock)
    std::atomic<bool> connected{false}; // Connection state (atomic for thread safety)
    SocketFd socketFd = -1;             // Socket file descriptor

    // Diagnostics-only state (atomic to avoid extra locking in send threads)
    std::atomic<bool> diagSendInFlight{false};
    std::atomic<uint64_t> diagInFlightMessageIdVal{0};
    std::atomic<int64_t> diagInFlightStartMsVal{0};
    std::atomic<uint64_t> diagLastSendCompletedMessageIdVal{0};
    std::atomic<int64_t> diagLastSendEndMsVal{0};

    // Runtime info state for quality-aware sending
    mutable std::mutex runtimeInfoMutex; // only used by the refresh writer
    std::chrono::steady_clock::time_point lastRefreshTime;
    TcpConnectionRuntimeInfo lastRuntimeInfo;  // read via seqlock (runtimeInfoGeneration)
    mutable std::atomic<uint32_t> runtimeInfoGeneration{0}; // seqlock generation for lock-free reads
    bool runtimeInfoStale = false;
};

typedef std::shared_ptr<TcpConnection> TcpConnectionSp;
