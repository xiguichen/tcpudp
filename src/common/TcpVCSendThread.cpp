#include "TcpVCSendThread.h"
#include "Log.h"
#include "Socket.h"
#include <algorithm>
#include <chrono>
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
    {
        lastRuntimeRefresh[i] = now;
        // Ensure sockets are non-blocking so SendTcpDirect returns immediately on EAGAIN.
        if (connections[i] && connections[i]->isConnected())
            SetSocketNonBlocking(connections[i]->getSocketFd());
    }
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

    // Exclude connections that are still within their degradation backoff window.
    // isCurrentlyDegraded() uses atomic reads and is safe to call from any thread.
    if (connIndex < socketStatuses.size() && socketStatuses[connIndex] &&
        socketStatuses[connIndex]->isCurrentlyDegraded(std::chrono::milliseconds(5000)))
    {
        return -1;
    }

    return score > 0 ? score : 0;
}

void TcpVCSendThread::run()
{
    log_info("TcpVCSendThread started");

    const size_t numConns = connections.size();

    // Scores are recomputed per packet but the storage is reused.
    std::vector<int> scores(numConns);
    // Indices sorted by score, reused across iterations.
    std::vector<size_t> order(numConns);

    while (this->isRunning())
    {
        auto dataVec = sendQueue->dequeue();
        if (!dataVec)
        {
            log_debug("Send thread exiting on null data (queue cancelled)");
            break;
        }
        if (!this->isRunning()) break;

        if (numConns == 0 || !connections[0]) continue;

        const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(dataVec->data());
        uint64_t messageId = packet->header.messageId;

        // Rate all connections.
        int bestScore = -1;
        for (size_t i = 0; i < numConns; i++)
        {
            scores[i] = rateConnection(i);
            if (scores[i] > bestScore)
                bestScore = scores[i];
        }

        // Build an order of indices to try. Start from roundRobinStart so that
        // equal-scored connections (the common case on Windows where TCP_INFO is
        // unavailable) are used in rotation rather than always hitting index 0.
        for (size_t i = 0; i < numConns; i++)
            order[i] = (roundRobinStart + i) % numConns;
        roundRobinStart = (roundRobinStart + 1) % numConns;

        // Stable-ish descending sort by score. Only the first few matter, so
        // partial_sort would save work, but with N=32 full sort is ~160 comparisons
        // which is fine and the code is clearer.
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return scores[a] > scores[b]; });

        // Fallback: if degradation/backoff eliminated all candidates, allow any connected socket.
        bool anyEligible = (bestScore >= 0);

        bool sent = false;
        for (size_t rank = 0; rank < numConns; rank++)
        {
            size_t idx = order[rank];
            if (!anyEligible)
            {
                if (!connections[idx] || !connections[idx]->isConnected())
                    continue;
            }
            else if (scores[idx] < 0)
            {
                continue;
            }

            auto &conn = connections[idx];
            conn->diagMarkSendStart(messageId);

            // Use direct send (no redundant poll) — socket is already non-blocking.
            ssize_t n = SendTcpDirect(conn->getSocketFd(), dataVec->data(), dataVec->size(), 0);

            conn->diagMarkSendEnd(messageId);

            if (n > 0)
            {
                if (messageTracker)
                    messageTracker->recordMessage(messageId, static_cast<int>(idx));

                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
                if (idx < connSendStats.size() && connSendStats[idx])
                {
                    auto &stats = connSendStats[idx];
                    stats->lastTxMessageId.store(messageId);
                    stats->lastTxTimeMs.store(nowMs);
                    stats->txCount.fetch_add(1);
                    stats->valid.store(true);
                    // Reset failure counter on success so a recovered connection
                    // regains a fair score over time.
                    stats->reenqueueCount.store(0, std::memory_order_relaxed);
                }

                if (idx < socketStatuses.size())
                    socketStatuses[idx]->recover();

                sent = true;
                break;
            }
            else
            {
                if (idx < socketStatuses.size())
                    socketStatuses[idx]->markDegraded();
                if (idx < connSendStats.size() && connSendStats[idx])
                    connSendStats[idx]->reenqueueCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (!sent)
        {
            sendQueue->enqueue(dataVec);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    log_info("TcpVCSendThread stopped");
}
