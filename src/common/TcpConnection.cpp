#include "TcpConnection.h"
#include "Log.h"
#include "PerformanceCounter.h"
#include "Socket.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

TcpConnection::~TcpConnection()
{
    if (socketFd != -1)
    {
        SocketClose(socketFd);
        socketFd = -1;
    }
}

static int64_t NowSteadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
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
        if (!connected.load())
        {
            return;
        }

        if (socketFd != -1)
        {
            SocketShutdown(socketFd, SHUT_RDWR);
            SocketClose(socketFd);
            socketFd = -1;
        }
        connected.store(false);
        log_debug("Resetting BlockingQueue post-disconnect.");
        // Capture callback to invoke outside the lock to prevent deadlock
        callbackToInvoke = disconnectCallback;
    }
    // Invoke callback after the lock is released
    if (callbackToInvoke)
    {
        callbackToInvoke();
    }
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
        size_t totalSent = 0;
        while (totalSent < size)
        {
            auto bytesSent = SendTcpData(socketFd, data + totalSent, size - totalSent, 0);
            if (bytesSent <= 0)
            {
                log_error("Failed to send data over TCP connection");
                SocketLogLastError();
                disconnect();
                return;
            }
            totalSent += bytesSent;
        }
    }
    else
    {
        log_error("Attempted to send data on a disconnected TCP connection");
    }
}

SocketFd TcpConnection::getSocketFd() const
{
    return socketFd;
}

TcpConnection::TcpConnectionRuntimeInfo TcpConnection::sampleRuntimeInfo()
{
    TcpConnectionRuntimeInfo info = {};

    if (!connected.load())
    {
        return info;
    }

#ifdef _WIN32
    // Windows-specific TCP telemetry
    // TCP_INFO is not reliably available across all Windows SDK versions
    // We'll rely on the fallback values
    // NOTE: On Windows all metrics remain zero (supported=false), so rateConnection() in
    // TcpVCSendThread scores every connection equally at 10000.  The only differentiator
    // is reenqueueCount.  A future improvement could use GetExtendedTcpTable or track
    // per-connection send timing to produce meaningful scores on Windows.
    info.supported = false;
    info.valid = false;
    info.isInExponentialBackoff = false;
    info.rtoUs = 0;
    info.smoothedRttUs = 0;
    info.congestionWindowBytes = 0;
    info.bytesInFlight = 0;
    info.retransmissionIndicator = 0;
    info.retransmissionIndicatorIsBytes = false;
    info.timeoutEpisodes = 0;
#elif defined(__APPLE__)
    // macOS: use TCP_CONNECTION_INFO instead of TCP_INFO
    struct tcp_connection_info tcpInfo = {};
    socklen_t tcpInfoLen = sizeof(tcpInfo);

    if (getsockopt(socketFd, IPPROTO_TCP, TCP_CONNECTION_INFO, &tcpInfo, &tcpInfoLen) == 0)
    {
        info.supported = true;
        info.valid = true;

        info.congestionWindowBytes = tcpInfo.tcpi_snd_cwnd;
        info.smoothedRttUs = tcpInfo.tcpi_srtt * 1000; // ms to us
        info.rtoUs = tcpInfo.tcpi_rto * 1000;          // ms to us
        // Bytes queued in the send buffer (data sent-but-unacked + waiting to send).
        // Previously this mistakenly used tcpi_snd_cwnd (the congestion *window*),
        // which made the bytesInFlight load penalty in rateConnection() meaningless on
        // macOS. tcpi_snd_sbbytes is the actual send-buffer occupancy, so a backed-up
        // connection now scores lower and the sender steers traffic to idle ones.
        info.bytesInFlight = tcpInfo.tcpi_snd_sbbytes;
        info.timeoutEpisodes = tcpInfo.tcpi_txretransmitpackets;
        info.isInExponentialBackoff = false; // not directly available on macOS
    }
#else
    // Linux: use getsockopt with TCP_INFO
    tcp_info tcpInfo = {};
    socklen_t tcpInfoLen = sizeof(tcpInfo);

    if (getsockopt(socketFd, IPPROTO_TCP, TCP_INFO, &tcpInfo, &tcpInfoLen) == 0)
    {
        info.supported = true;
        info.valid = true;

        // Note: tcpi_snd_cwnd is in MSS units (packets), not bytes
        info.congestionWindowBytes = tcpInfo.tcpi_snd_cwnd * tcpInfo.tcpi_snd_mss;
        info.smoothedRttUs = tcpInfo.tcpi_rtt;
        info.rtoUs = tcpInfo.tcpi_rto * 1000;

        // Estimate bytes in flight using actual MSS
        info.bytesInFlight = tcpInfo.tcpi_unacked * tcpInfo.tcpi_snd_mss;

        // Timeout episodes
        info.timeoutEpisodes = tcpInfo.tcpi_retransmits;

        // Exponential backoff (retransmits) indicates real congestion
        info.isInExponentialBackoff = (tcpInfo.tcpi_backoff > 1);
    }
#endif

    return info;
}

bool TcpConnection::refreshRuntimeInfoIfStale(std::chrono::milliseconds refreshInterval)
{
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(runtimeInfoMutex);

    if ((now - lastRefreshTime) < refreshInterval)
    {
        return false; // Not stale
    }

    lastRefreshTime = now;
    runtimeInfoStale = false;

    auto info = sampleRuntimeInfo();

    // Seqlock write: bump generation (odd = write in progress), store, bump again (even = consistent).
    runtimeInfoGeneration.fetch_add(1, std::memory_order_release);
    lastRuntimeInfo = info;
    runtimeInfoGeneration.fetch_add(1, std::memory_order_release);

    return true; // Was stale, now refreshed
}
