#include "TcpVCSendThread.h"
#include "Log.h"
#include "Socket.h"
#include <algorithm>
#include <chrono>
#include <thread>

using namespace Logger;

TcpVCSendThread::TcpVCSendThread(std::vector<TcpConnectionSp> connections_,
                                 BlockingQueueSp sendQueue_,
                                 BlockingQueueSp resendQueue_,
                                 std::vector<std::shared_ptr<ConnSendStats>> connSendStats_,
                                 std::shared_ptr<MessageTracker> messageTracker_,
                                 std::vector<std::shared_ptr<SocketStatus>> socketStatuses_)
    : connections(std::move(connections_)),
      sendQueue(std::move(sendQueue_)),
      resendQueue(std::move(resendQueue_)),
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
    // Only reserve VC_RESEND_CONN_INDEX for resend traffic when the channel
    // actually has that many connections.  Test channels with fewer connections
    // use all slots for normal data (sendOnResendConn handles the absent-conn case).
    const bool hasResendConn = (numConns > VC_RESEND_CONN_INDEX);
    const size_t numDataConns = hasResendConn ? static_cast<size_t>(VC_RESEND_CONN_INDEX) : numConns;

    // Scores are recomputed per packet but the storage is reused.
    std::vector<int> scores(numDataConns);
    // Indices sorted by score, reused across iterations.
    std::vector<size_t> order(numDataConns);

    while (this->isRunning())
    {
        // Priority: drain resend queue first (non-blocking).
        if (resendQueue)
        {
            auto resendData = resendQueue->tryDequeue();
            if (resendData)
            {
                sendOnResendConn(std::move(resendData));
                continue; // immediately re-check resend queue
            }
        }

        // Block for normal data with a short timeout so the resend queue is
        // checked again even when no normal traffic is flowing.
        auto dataVec = sendQueue->dequeueWithTimeout(5);
        if (!dataVec)
            continue; // timeout — loop back to check resend queue
        if (!this->isRunning())
            break;

        if (numDataConns == 0 || !connections[0])
            continue;

        const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(dataVec->data());
        uint64_t messageId = packet->header.messageId;

        // Rate data connections only (indices 0..numDataConns-1).
        // VC_RESEND_CONN_INDEX is never included in the candidate pool.
        int bestScore = -1;
        for (size_t i = 0; i < numDataConns; i++)
        {
            scores[i] = rateConnection(i);
            if (scores[i] > bestScore)
                bestScore = scores[i];
        }

        // Build an order of indices to try. Start from roundRobinStart so that
        // equal-scored connections (the common case on Windows where TCP_INFO is
        // unavailable) are used in rotation rather than always hitting index 0.
        for (size_t i = 0; i < numDataConns; i++)
            order[i] = (roundRobinStart + i) % numDataConns;
        roundRobinStart = (roundRobinStart + 1) % numDataConns;

        // Stable-ish descending sort by score. Only the first few matter, so
        // partial_sort would save work, but with N=31 full sort is ~155 comparisons
        // which is fine and the code is clearer.
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return scores[a] > scores[b]; });

        // Fallback: if degradation/backoff eliminated all candidates, allow any connected socket.
        bool anyEligible = (bestScore >= 0);

        bool sent = false;
        for (size_t rank = 0; rank < numDataConns; rank++)
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
                // Handle partial send: on non-blocking sockets, send() may return
                // fewer bytes than requested when the kernel buffer is nearly full.
                // Once we've written ANY bytes to this connection we MUST complete
                // the packet here — switching connections mid-packet corrupts both
                // TCP streams (the receiver expects contiguous protocol bytes).
                size_t totalSent = static_cast<size_t>(n);
                while (totalSent < dataVec->size())
                {
                    if (!conn->isConnected() || !this->isRunning())
                        break;
                    // Wait up to 100ms for socket to become writable again.
                    if (!IsSocketWritable(conn->getSocketFd(), 100))
                        continue;
                    ssize_t r = SendTcpDirect(
                        conn->getSocketFd(),
                        dataVec->data() + totalSent,
                        dataVec->size() - totalSent, 0);
                    if (r > 0)
                        totalSent += static_cast<size_t>(r);
                    else if (r == SOCKET_ERROR_WOULD_BLOCK)
                        continue;
                    else
                        break; // connection error
                }

                if (totalSent < dataVec->size())
                {
                    // Connection died mid-packet. The partial bytes already in the
                    // stream make this packet unrecoverable on this connection.
                    // The VC reliability layer (resend on missing) handles recovery.
                    if (idx < socketStatuses.size())
                        socketStatuses[idx]->markDegraded();
                    if (idx < connSendStats.size() && connSendStats[idx])
                        connSendStats[idx]->reenqueueCount.fetch_add(1, std::memory_order_relaxed);
                    log_warnning("Partial send failed: connection died mid-packet on connection " +
                                std::to_string(idx) + " for msgId " + std::to_string(messageId));
                    continue; // try next connection for next packet (this one is lost)
                }

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

void TcpVCSendThread::sendOnResendConn(std::shared_ptr<std::vector<char>> data)
{
    if (connections.size() <= VC_RESEND_CONN_INDEX)
    {
        log_warnning("[RESEND] No resend connection available (connections.size()=" +
                     std::to_string(connections.size()) + ")");
        return;
    }

    auto &conn = connections[VC_RESEND_CONN_INDEX];
    const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(data->data());
    uint64_t messageId = packet->header.messageId;

    if (!conn || !conn->isConnected())
    {
        log_warnning("[RESEND] Resend connection (conn " + std::to_string(VC_RESEND_CONN_INDEX) +
                     ") unavailable, dropping msgId=" + std::to_string(messageId));
        return;
    }

    ssize_t n = SendTcpDirect(conn->getSocketFd(), data->data(), data->size(), 0);

    if (n > 0)
    {
        size_t totalSent = static_cast<size_t>(n);
        while (totalSent < data->size())
        {
            if (!conn->isConnected() || !this->isRunning())
                break;
            if (!IsSocketWritable(conn->getSocketFd(), 100))
                continue;
            ssize_t r = SendTcpDirect(conn->getSocketFd(),
                                      data->data() + totalSent,
                                      data->size() - totalSent, 0);
            if (r > 0)
                totalSent += static_cast<size_t>(r);
            else if (r == SOCKET_ERROR_WOULD_BLOCK)
                continue;
            else
                break; // connection error
        }

        if (totalSent < data->size())
        {
            if (VC_RESEND_CONN_INDEX < socketStatuses.size())
                socketStatuses[VC_RESEND_CONN_INDEX]->markDegraded();
            log_warnning("[RESEND] Resend connection died mid-packet for msgId=" +
                         std::to_string(messageId));
        }
        else
        {
            if (VC_RESEND_CONN_INDEX < socketStatuses.size())
                socketStatuses[VC_RESEND_CONN_INDEX]->recover();
            log_info("[RESEND] Sent resend msgId=" + std::to_string(messageId) +
                     " on conn " + std::to_string(VC_RESEND_CONN_INDEX));
        }
    }
    else
    {
        if (VC_RESEND_CONN_INDEX < socketStatuses.size())
            socketStatuses[VC_RESEND_CONN_INDEX]->markDegraded();
        log_warnning("[RESEND] Failed to send resend msgId=" + std::to_string(messageId) +
                     " on conn " + std::to_string(VC_RESEND_CONN_INDEX));
    }
}
