#include "TcpVCSendThread.h"
#include "Log.h"
#include "Socket.h"
#include <chrono>
#include <format>
#include <thread>

using namespace Logger;

TcpVCSendThread::TcpVCSendThread(std::vector<TcpConnectionSp> connections_,
                                 BlockingQueueSp sendQueue_,
                                 std::vector<std::shared_ptr<ConnSendStats>> connSendStats_,
                                 std::shared_ptr<MessageTracker> messageTracker_,
                                 std::vector<std::shared_ptr<SocketStatus>> socketStatuses_)
    : connections(std::move(connections_)),
      sendQueue(std::move(sendQueue_)),
      connSendStats(std::move(connSendStats_)),
      messageTracker(std::move(messageTracker_)),
      socketStatuses(std::move(socketStatuses_))
{
    lastRuntimeRefresh.resize(connections.size());
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < connections.size(); i++)
        lastRuntimeRefresh[i] = now;
}

TcpVCSendThread::~TcpVCSendThread() = default;

void TcpVCSendThread::refreshConnRuntimeInfo(size_t connIndex)
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

int TcpVCSendThread::rateConnection(size_t connIndex)
{
    if (connIndex >= connections.size())
        return -1;
    auto &conn = connections[connIndex];
    if (!conn || !conn->isConnected())
        return -1;

    refreshConnRuntimeInfo(connIndex);
    auto info = conn->getLastRuntimeInfo();

    if (info.isInExponentialBackoff)
        return -1;

    int score = 10000;
    score += static_cast<int>(info.congestionWindowBytes / 100);
    score -= static_cast<int>(info.smoothedRttUs / 500);
    score -= static_cast<int>(info.bytesInFlight / 500);
    score -= info.timeoutEpisodes * 200;

    if (connIndex < connSendStats.size() && connSendStats[connIndex])
    {
        auto &stats = connSendStats[connIndex];
        uint64_t failCount = stats->reenqueueCount.load(std::memory_order_relaxed);
        score -= static_cast<int>(failCount) * 200;
    }

    return score > 0 ? score : 0;
}

void TcpVCSendThread::run()
{
    log_info("TcpVCSendThread started");

    while (this->isRunning())
    {
        auto dataVec = sendQueue->dequeue();
        if (!dataVec)
        {
            log_debug("Send thread exiting on null data (queue cancelled)");
            break;
        }
        if (!this->isRunning()) break;

        if (!connections.empty() && connections[0])
        {
            const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(dataVec->data());
            uint64_t messageId = packet->header.messageId;
            uint16_t payloadLen = packet->dataLength;

            bool sent = false;

            for (size_t trial = 0; trial < 2 && !sent; trial++)
            {
                int bestIndex = -1;
                int bestScore = -1;

                for (size_t i = 0; i < connections.size(); i++)
                {
                    if (trial == 0 && socketStatuses[i]->isCurrentlyDegraded(std::chrono::milliseconds(500)))
                        continue;

                    int score = rateConnection(static_cast<int>(i));
                    if (score < 0) continue;

                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestIndex = static_cast<int>(i);
                    }
                }

                if (bestIndex < 0)
                {
                    if (trial == 0)
                    {
                        log_debug("No non-degraded connection available, trying degraded");
                        continue;
                    }
                    log_warnning("All connections unavailable for send, re-enqueuing");
                    sendQueue->enqueue(dataVec);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    break;
                }

                auto &conn = connections[bestIndex];
                conn->diagMarkSendStart(messageId);

                ssize_t n = SendTcpDataNonBlocking(conn->getSocketFd(), dataVec->data(), dataVec->size(), 0, 0);

                conn->diagMarkSendEnd(messageId);

                if (n > 0)
                {
                    if (messageTracker)
                        messageTracker->recordMessage(messageId, bestIndex);

                    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count();
                    if (static_cast<size_t>(bestIndex) < connSendStats.size() && connSendStats[bestIndex])
                    {
                        auto &stats = connSendStats[bestIndex];
                        stats->lastTxMessageId.store(messageId);
                        stats->lastTxTimeMs.store(nowMs);
                        stats->txCount.fetch_add(1);
                        stats->valid.store(true);
                    }

                    sent = true;
                }
                else
                {
                    log_debug(std::format("Send blocked on conn={} messageId={}, marking degraded", bestIndex, messageId));
                    if (static_cast<size_t>(bestIndex) < socketStatuses.size())
                        socketStatuses[bestIndex]->markDegraded();
                    if (static_cast<size_t>(bestIndex) < connSendStats.size() && connSendStats[bestIndex])
                        connSendStats[bestIndex]->reenqueueCount.fetch_add(1, std::memory_order_relaxed);

                    if (trial == 1)
                    {
                        sendQueue->enqueue(dataVec);
                        log_warnning(std::format("Send blocked on all connections for messageId={}, re-enqueuing", messageId));
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
            }
        }
    }

    log_info("TcpVCSendThread stopped");
}
