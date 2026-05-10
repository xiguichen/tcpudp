#include "TcpVCIoThread.h"
#include "Log.h"
#include "Socket.h"
#include <chrono>
#include <format>
#include <thread>

using namespace Logger;

static constexpr int IO_POLL_TIMEOUT_MS = 50;

TcpVCIoThread::TcpVCIoThread(std::vector<TcpConnectionSp> connections_,
                             BlockingQueueSp sendQueue_,
                             std::vector<std::shared_ptr<ConnSendStats>> connSendStats_,
                             std::shared_ptr<MessageTracker> messageTracker_,
                             std::vector<std::shared_ptr<SocketStatus>> socketStatuses_,
                             std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback_,
                             std::function<void(uint64_t)> resendRequestCallback_,
                             std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback_,
                             std::function<void(TcpConnectionSp)> disconnectCallback_)
    : connections(std::move(connections_)),
      sendQueue(std::move(sendQueue_)),
      connSendStats(std::move(connSendStats_)),
      messageTracker(std::move(messageTracker_)),
      socketStatuses(std::move(socketStatuses_)),
      dataCallback(std::move(dataCallback_)),
      resendRequestCallback(std::move(resendRequestCallback_)),
      missingNotifyCallback(std::move(missingNotifyCallback_)),
      disconnectCallback(std::move(disconnectCallback_))
{
    readBuffers.resize(connections.size());
    lastRuntimeRefresh.resize(connections.size());
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < connections.size(); i++)
    {
        auto &conn = connections[i];
        lastRuntimeRefresh[i] = now;
        if (conn && conn->isConnected())
        {
            SocketFd fd = conn->getSocketFd();
            if (fd != -1)
            {
                SetSocketNonBlocking(fd);
            }
        }
    }
}

void TcpVCIoThread::refreshConnRuntimeInfo(size_t connIndex)
{
    if (connIndex >= connections.size()) return;
    auto &conn = connections[connIndex];
    if (!conn || !conn->isConnected()) return;

    auto now = std::chrono::steady_clock::now();
    if (now - lastRuntimeRefresh[connIndex] < std::chrono::milliseconds(TCP_RUNTIME_REFRESH_MS))
        return;

    lastRuntimeRefresh[connIndex] = now;
    conn->refreshRuntimeInfoIfStale(std::chrono::milliseconds(TCP_RUNTIME_REFRESH_MS));
}

TcpVCIoThread::~TcpVCIoThread() = default;

void TcpVCIoThread::run()
{
    log_info("TcpVCIoThread started");

    size_t numConns = connections.size();
    std::vector<struct pollfd> pollfds(numConns);

    while (this->isRunning())
    {
        bool hasDataToSend = sendQueue && sendQueue->size() > 0;

        for (size_t i = 0; i < numConns; i++)
        {
            if (connections[i] && connections[i]->isConnected())
            {
                pollfds[i].fd = connections[i]->getSocketFd();
                pollfds[i].events = POLLIN;
                if (hasDataToSend)
                    pollfds[i].events |= POLLOUT;
                pollfds[i].revents = 0;
            }
            else
            {
                pollfds[i].fd = -1;
                pollfds[i].events = 0;
                pollfds[i].revents = 0;
            }
        }

        int pollTimeout = hasDataToSend ? 0 : IO_POLL_TIMEOUT_MS;
        int ready = SocketPollMany(pollfds.data(), numConns, pollTimeout);
        if (ready < 0)
        {
            log_error("SocketPollMany failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        drainSendQueue(pollfds, true);

        for (size_t i = 0; i < numConns; i++)
        {
            if (pollfds[i].fd < 0) continue;

            if (pollfds[i].revents & (POLLIN | POLLERR | POLLHUP))
            {
                readFromConnection(static_cast<int>(i));
                if (!this->isRunning()) return;
            }
        }

        drainSendQueue(pollfds, false);

        if (ready == 0 && (!sendQueue || sendQueue->size() == 0))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    log_info("TcpVCIoThread stopped");
}

void TcpVCIoThread::drainSendQueue(const std::vector<struct pollfd> &pollfds, bool onlyNonDegraded)
{
    if (!sendQueue || sendQueue->size() == 0) return;
    size_t numConns = connections.size();

    auto reenqueueData = [this](std::shared_ptr<std::vector<char>> dataVec, size_t connIdx) {
        if (connIdx < connSendStats.size() && connSendStats[connIdx])
            connSendStats[connIdx]->reenqueueCount.fetch_add(1, std::memory_order_relaxed);
        sendQueue->enqueue(dataVec);
    };

    for (size_t i = 0; i < numConns; i++)
    {
        if (pollfds[i].fd < 0) continue;
        if (!(pollfds[i].revents & POLLOUT)) continue;

        if (onlyNonDegraded && socketStatuses[i]->isCurrentlyDegraded(std::chrono::milliseconds(500)))
        {
            log_debug(std::format("Connection {} degraded, skipping", i));
            continue;
        }

        while (sendQueue->size() > 0)
        {
            auto dataVec = sendQueue->tryDequeue();
            if (!dataVec) break;

            refreshConnRuntimeInfo(i);

            if (onlyNonDegraded && socketStatuses[i]->isCurrentlyDegraded(std::chrono::milliseconds(500)))
            {
                log_debug(std::format("Connection {} degraded, re-enqueuing", i));
                reenqueueData(dataVec, i);
                continue;
            }

            auto runtimeInfo = connections[i]->getLastRuntimeInfo();
            if (onlyNonDegraded && runtimeInfo.isInExponentialBackoff)
            {
                log_warnning(std::format("Connection {} in exponential backoff, marking degraded and re-enqueuing", i));
                socketStatuses[i]->markDegraded();
                reenqueueData(dataVec, i);
                continue;
            }

            if (!sendFromConnection(static_cast<int>(i), dataVec))
            {
                reenqueueData(dataVec, i);
                break;
            }
        }

        if (sendQueue->size() == 0) break;
    }
}

bool TcpVCIoThread::sendFromConnection(int connIndex, std::shared_ptr<std::vector<char>> dataVec)
{
    if (connIndex < 0 || static_cast<size_t>(connIndex) >= connections.size())
        return false;

    auto &conn = connections[connIndex];
    if (!conn || !conn->isConnected()) return false;

    const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(dataVec->data());
    uint64_t messageId = packet->header.messageId;
    uint16_t payloadLen = packet->dataLength;

    auto sendStart = std::chrono::steady_clock::now();
    conn->diagMarkSendStart(messageId);

    ssize_t sent = SendTcpDataNonBlocking(conn->getSocketFd(), dataVec->data(), dataVec->size(), 0, 0);

    conn->diagMarkSendEnd(messageId);
    auto sendDur = std::chrono::steady_clock::now() - sendStart;

    if (sent <= 0)
    {
        if (static_cast<size_t>(connIndex) < connSendStats.size() && connSendStats[connIndex])
            connSendStats[connIndex]->slowSendCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (messageTracker)
    {
        messageTracker->recordMessage(messageId, connIndex);
    }

    if (sendDur >= SLOW_SEND_WARN_MS && static_cast<size_t>(connIndex) < connSendStats.size() && connSendStats[connIndex])
    {
        connSendStats[connIndex]->slowSendCount.fetch_add(1, std::memory_order_relaxed);
        auto durMs = std::chrono::duration_cast<std::chrono::milliseconds>(sendDur).count();
        log_warnning(std::format("Slow TCP send: conn={} messageId={} payload={}B total={}B sendMs={}",
                                 connIndex, messageId, payloadLen, dataVec->size(), durMs));
    }

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch())
                     .count();

    if (static_cast<size_t>(connIndex) < connSendStats.size())
    {
        auto &stats = connSendStats[connIndex];
        if (stats)
        {
            stats->lastTxMessageId.store(messageId);
            stats->lastTxTimeMs.store(nowMs);
            stats->txCount.fetch_add(1);
            stats->valid.store(true);
        }
    }

    return true;
}

void TcpVCIoThread::readFromConnection(int connIndex)
{
    if (connIndex < 0 || static_cast<size_t>(connIndex) >= connections.size())
        return;

    auto &conn = connections[connIndex];
    if (!conn || !conn->isConnected()) return;

    auto &buf = readBuffers[connIndex];

    char temp[1500];

    while (this->isRunning())
    {
        ssize_t n = RecvTcpDataNonBlocking(conn->getSocketFd(), temp, sizeof(temp), 0, 0);
        if (n > 0)
        {
            buf.data.insert(buf.data.end(), temp, temp + n);
        }
        else if (n == SOCKET_ERROR_CLOSED)
        {
            log_info("Connection closed");
            if (disconnectCallback)
                disconnectCallback(conn);
            return;
        }
        else if (n == SOCKET_ERROR_WOULD_BLOCK || n == SOCKET_ERROR_TIMEOUT)
        {
            break;
        }
        else
        {
            log_error("Read error on connection");
            if (disconnectCallback)
                disconnectCallback(conn);
            return;
        }
    }

    while (buf.data.size() >= VC_MIN_DATA_PACKET_SIZE)
    {
        uint8_t packetType = static_cast<uint8_t>(buf.data[0]);
        switch (static_cast<VcPacketType>(packetType))
        {
        case VcPacketType::DATA:
        {
            if (buf.data.size() < sizeof(VCDataPacket))
                return;
            VCDataPacket *pkt = reinterpret_cast<VCDataPacket *>(buf.data.data());
            if (pkt->dataLength > VC_MAX_DATA_PAYLOAD_SIZE)
                return;
            size_t totalSize = sizeof(VCDataPacket) + pkt->dataLength;
            if (buf.data.size() < totalSize)
                return;

            auto data = std::make_shared<std::vector<char>>(pkt->data, pkt->data + pkt->dataLength);
            if (dataCallback)
                dataCallback(pkt->header.messageId, data, connIndex);
            buf.data.erase(buf.data.begin(), buf.data.begin() + static_cast<long>(totalSize));
            break;
        }
        case VcPacketType::RESEND_REQUEST:
        {
            if (buf.data.size() < sizeof(VCResendRequest))
                return;
            VCResendRequest *req = reinterpret_cast<VCResendRequest *>(buf.data.data());
            if (resendRequestCallback)
                resendRequestCallback(req->missingMessageId);
            buf.data.erase(buf.data.begin(), buf.data.begin() + sizeof(VCResendRequest));
            break;
        }
        case VcPacketType::MISSING_NOTIFY:
        {
            if (buf.data.size() < sizeof(VCHeader) + 1)
                return;
            VCMissingNotify *notify = reinterpret_cast<VCMissingNotify *>(buf.data.data());
            size_t expectedSize = sizeof(VCHeader) + 1 + notify->count * sizeof(uint64_t);
            if (buf.data.size() < expectedSize)
                return;

            std::vector<uint64_t> missingIds(notify->missingIds, notify->missingIds + notify->count);
            if (missingNotifyCallback)
                missingNotifyCallback(missingIds);
            buf.data.erase(buf.data.begin(), buf.data.begin() + static_cast<long>(expectedSize));
            break;
        }
        default:
            log_error(std::format("Unknown packet type: {}", packetType));
            buf.data.clear();
            return;
        }
    }
}
