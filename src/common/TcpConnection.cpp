#include "TcpConnection.h"
#include "Log.h"
#include "PerformanceCounter.h"
#include "Socket.h"
#include <chrono>
#include <mutex>
#include <cstring>
#include <algorithm>

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
    info.supported = false;
    info.valid = false;
    info.isCongested = false;
    info.isInExponentialBackoff = false;
    info.rtoUs = 0;
    info.rtoIsApproximate = false;
    info.smoothedRttUs = 0;
    info.congestionWindowBytes = 0;
    info.bytesInFlight = 0;
    info.retransmissionIndicator = 0;
    info.retransmissionIndicatorIsBytes = false;
    info.timeoutEpisodes = 0;
#else
    // Linux/macOS: use getsockopt with TCP_INFO
    // Note: This requires CAP_NET_ADMIN or running as root on some systems
    tcp_info tcpInfo = {};
    socklen_t tcpInfoLen = sizeof(tcpInfo);

    if (getsockopt(socketFd, IPPROTO_TCP, TCP_INFO, &tcpInfo, &tcpInfoLen) == 0)
    {
        info.supported = true;
        info.valid = true;

        // Map Linux TCP_INFO to our runtime info structure
        info.congestionWindowBytes = tcpInfo.tcpi_rtt;
        info.smoothedRttUs = tcpInfo.tcpi_rttvar * 1000;
        info.rtoUs = tcpInfo.tcpi_rto * 1000;

        // Estimate bytes in flight (simplified)
        info.bytesInFlight = tcpInfo.tcpi_snd_cwnd * 1460; // Assuming 1460 MTU

        // RTO is approximate (not exact) on Linux
        info.rtoIsApproximate = true;

        // Timeout episodes
        info.timeoutEpisodes = tcpInfo.tcpi_retransmits;

        // Determine congestion state
        info.isCongested = (info.congestionWindowBytes > 0 && info.bytesInFlight > info.congestionWindowBytes);
        info.isInExponentialBackoff = (info.timeoutEpisodes > 0);
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
    lastRuntimeInfo = sampleRuntimeInfo();

    return true; // Was stale, now refreshed
}

TcpConnection::TcpConnectionRuntimeInfo TcpConnection::getLastRuntimeInfo() const
{
    std::lock_guard<std::mutex> lock(runtimeInfoMutex);
    return lastRuntimeInfo;
}

