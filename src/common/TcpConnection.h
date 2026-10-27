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

    ~TcpConnection();

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

    // Get the socket file descriptor. The value is advisory: it may become -1
    // concurrently (disconnect()). Callers that actually perform I/O must use
    // sendDirect()/recvDirect()/waitWritable(), which hold fdMutex across the
    // syscall so the FD cannot be closed and recycled underneath them.
    SocketFd getSocketFd() const;

    // Non-blocking I/O helpers. They serialise with disconnect() so another
    // thread can never close (and the OS reuse) the descriptor between our
    // liveness check and the actual send()/recv(). This is the close-vs-I/O
    // race that otherwise lets one dead slot silently write into an unrelated
    // connection after its FD number is recycled.
    ssize_t sendDirect(const void *data, size_t length, int flags = 0);
    ssize_t recvDirect(void *buffer, size_t bufferSize, int flags = 0);
    // Wait until the socket is writable, using a snapshot taken under fdMutex.
    bool waitWritable(int timeoutMs);

  private:
    std::mutex disconnectMutex; // Per-instance mutex for disconnect (not static — avoids cross-instance deadlock)
    std::mutex fdMutex;         // Serialises syscalls against disconnect()/close()
    std::atomic<bool> connected{false}; // Connection state (atomic for thread safety)
    std::atomic<SocketFd> socketFd{-1}; // Socket file descriptor (atomic: read while disconnect() closes)

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
