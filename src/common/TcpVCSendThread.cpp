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

    // Wake the run() idle wait whenever either queue receives work, so the thread
    // no longer has to poll the resend queue on a short timer. The notifier captures
    // only the shared Waker (not this), so it stays safe if the thread is destroyed
    // while a queue still references it. The brief lock on waker->mtx closes the
    // lost-wakeup window against run()'s wait_for predicate.
    waker = std::make_shared<Waker>();
    auto wake = [w = waker]() {
        {
            std::lock_guard<std::mutex> lk(w->mtx);
        }
        w->cv.notify_one();
    };
    if (sendQueue)
        sendQueue->setEnqueueNotifier(wake);
    if (resendQueue)
        resendQueue->setEnqueueNotifier(wake);
}

TcpVCSendThread::~TcpVCSendThread()
{
    // Stop the queues from calling into our Waker once we're gone. Safe to call
    // concurrently with enqueue() (guarded by the queue mutex); any in-flight
    // notifier already holds its own shared_ptr to the Waker.
    if (sendQueue)
        sendQueue->setEnqueueNotifier(nullptr);
    if (resendQueue)
        resendQueue->setEnqueueNotifier(nullptr);
}

void TcpVCSendThread::replaceConnection(int slot, TcpConnectionSp conn,
                                        std::shared_ptr<ConnSendStats> stats,
                                        std::shared_ptr<SocketStatus> status)
{
    if (slot < 0 || static_cast<size_t>(slot) >= connections.size())
        return;
    SetSocketNonBlocking(conn->getSocketFd());
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        connections[slot] = conn;
        if (static_cast<size_t>(slot) < connSendStats.size())
            connSendStats[slot] = stats;
        if (static_cast<size_t>(slot) < socketStatuses.size())
            socketStatuses[slot] = status;
        if (static_cast<size_t>(slot) < lastRuntimeRefresh.size())
            lastRuntimeRefresh[slot] = std::chrono::steady_clock::now();
    }
    connAvailableCv.notify_one();
}

void TcpVCSendThread::setRunning(bool running)
{
    StopableThread::setRunning(running);
    if (!running)
    {
        connAvailableCv.notify_one();
        // Also wake the idle work-wait so shutdown is observed promptly.
        if (waker)
        {
            {
                std::lock_guard<std::mutex> lk(waker->mtx);
            }
            waker->cv.notify_one();
        }
    }
}

void TcpVCSendThread::refreshConnRuntimeInfo(size_t connIndex,
                                              const std::vector<TcpConnectionSp>& conns)
{
    if (connIndex >= conns.size()) return;
    const auto &conn = conns[connIndex];
    if (!conn || !conn->isConnected()) return;

    auto now = std::chrono::steady_clock::now();
    if (now - lastRuntimeRefresh[connIndex] < std::chrono::milliseconds(TCP_RUNTIME_REFRESH_MS))
        return;

    lastRuntimeRefresh[connIndex] = now;
    conn->refreshRuntimeInfoIfStale(std::chrono::milliseconds(TCP_RUNTIME_REFRESH_MS));
}

int TcpVCSendThread::rateConnection(size_t connIndex,
                                    const std::vector<TcpConnectionSp>& conns,
                                    const std::vector<std::shared_ptr<ConnSendStats>>& stats)
{
    if (connIndex >= conns.size())
        return -1;
    const auto &conn = conns[connIndex];
    if (!conn || !conn->isConnected())
        return -1;

    refreshConnRuntimeInfo(connIndex, conns);
    auto info = conn->getLastRuntimeInfo();

    if (info.isInExponentialBackoff)
        return -1;

    int score = 10000;
    score += static_cast<int>(info.congestionWindowBytes / 100);
    score -= static_cast<int>(info.smoothedRttUs / 500);
    score -= static_cast<int>(info.bytesInFlight / 500);
    score -= info.timeoutEpisodes * 200;

    if (connIndex < stats.size() && stats[connIndex])
    {
        uint64_t failCount = stats[connIndex]->reenqueueCount.load(std::memory_order_relaxed);
        score -= static_cast<int>(failCount) * 200;
    }

    return score > 0 ? score : 0;
}

void TcpVCSendThread::run()
{
    log_info("TcpVCSendThread started");

    const size_t numConns = connections.size();
    // Reserve resend slots only when the channel has enough connections.
    // Test channels with fewer connections use all slots for normal data
    // (sendOnResendConn handles the absent-conn case).
    const bool hasResendConns = (numConns > VC_FIRST_RESEND_CONN_INDEX);
    const size_t numDataConns = hasResendConns ? static_cast<size_t>(VC_FIRST_RESEND_CONN_INDEX) : numConns;

    // Scores are recomputed per packet but the storage is reused.
    std::vector<int> scores(numDataConns);
    // Indices sorted by score, reused across iterations.
    std::vector<size_t> order(numDataConns);

    // Packet retained across iterations when all connections fail — avoids re-enqueuing
    // (which inflates approxSize and causes spurious drops).
    std::shared_ptr<std::vector<char>> pendingDataVec;

    while (this->isRunning())
    {
        // Priority: drain resend queue first (non-blocking), but batch-limit
        // to avoid starving normal sends when the resend queue is busy.
        // anyResendAlive is reused by the idle wait below to decide whether
        // pending resend work is actually drainable right now.
        bool anyResendAlive = false;
        if (resendQueue)
        {
            // Quick check: are ANY resend connections alive? If none are,
            // skip draining to avoid a hot re-enqueue loop while the
            // watchdog reconnects.
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                for (size_t i = VC_FIRST_RESEND_CONN_INDEX; i < connections.size(); i++)
                {
                    if (connections[i] && connections[i]->isConnected())
                    {
                        anyResendAlive = true;
                        break;
                    }
                }
            }

            if (anyResendAlive)
            {
                size_t resendBatch = 0;
                while (resendBatch < MAX_RESEND_BATCH)
                {
                    auto resendData = resendQueue->tryDequeue();
                    if (!resendData)
                        break;
                    std::vector<TcpConnectionSp> connSnap;
                    {
                        std::lock_guard<std::mutex> lock(connectionsMutex);
                        connSnap = connections;
                    }
                    sendOnResendConn(std::move(resendData), connSnap);
                    resendBatch++;
                }
            }

            // Prevent unbounded growth of the retry tracker map.
            if (resendRetryCount.size() > MAX_RESEND_TRACKED)
            {
                log_warnning("[RESEND] Retry tracker has " +
                             std::to_string(resendRetryCount.size()) +
                             " entries, clearing stale entries");
                resendRetryCount.clear();
            }
        }

        // Use retained pending packet or dequeue a new one (non-blocking).
        std::shared_ptr<std::vector<char>> dataVec;
        if (pendingDataVec)
        {
            dataVec = std::move(pendingDataVec);
        }
        else
        {
            dataVec = sendQueue->tryDequeue();
        }
        if (!dataVec)
        {
            // No normal-send work right now. Block until there is work or shutdown.
            // The queues' enqueue notifier wakes us instantly, so send/resend latency
            // is unchanged in the common case; IDLE_WAIT_MS is only a backstop. This
            // replaces the old 5ms busy-poll that woke the thread ~200x/sec per
            // channel even when fully idle.
            //
            // Resend work only counts as wakeable when a resend connection is alive
            // to drain it. Otherwise (the watchdog-reconnect window) pending resend
            // items would keep the predicate true and spin the CPU; instead we fall
            // back to the IDLE_WAIT_MS backstop and re-check liveness next iteration.
            const bool resendDrainable = anyResendAlive;
            std::unique_lock<std::mutex> lock(waker->mtx);
            waker->cv.wait_for(lock, std::chrono::milliseconds(IDLE_WAIT_MS), [&] {
                return !this->isRunning() ||
                       (sendQueue && sendQueue->approxSize() > 0) ||
                       (resendDrainable && resendQueue && resendQueue->approxSize() > 0);
            });
            continue; // loop back: drain resend first, then re-dequeue
        }
        if (!this->isRunning())
            break;

        // Take a consistent snapshot of connections + stats under the mutex.
        // This ensures replaceConnection() writes are visible for this packet —
        // no stale pointers during scoring/sending.
        std::vector<TcpConnectionSp> connSnap;
        std::vector<std::shared_ptr<ConnSendStats>> statsSnap;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            connSnap = connections;
            statsSnap = connSendStats;
        }

        if (numDataConns == 0 || !connSnap[0])
            continue;

        const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(dataVec->data());
        uint64_t messageId = packet->header.messageId;

        // Rate data connections only (indices 0..numDataConns-1).
        // Resend connections (VC_FIRST_RESEND_CONN_INDEX..VC_TCP_CONNECTIONS-1) are excluded.
        int bestScore = -1;
        bool uniformScores = true;
        for (size_t i = 0; i < numDataConns; i++)
        {
            scores[i] = rateConnection(i, connSnap, statsSnap);
            if (i > 0 && scores[i] != scores[0])
                uniformScores = false;
            if (scores[i] > bestScore)
                bestScore = scores[i];
        }

        // Build an order of indices to try. Start from roundRobinStart so that
        // equal-scored connections (the common case on Windows where TCP_INFO is
        // unavailable) are used in rotation rather than always hitting index 0.
        for (size_t i = 0; i < numDataConns; i++)
            order[i] = (roundRobinStart + i) % numDataConns;
        roundRobinStart = (roundRobinStart + 1) % numDataConns;

        // Sort by score only when scores actually differ. On Windows TCP_INFO is
        // unavailable, so every live connection scores identically and the sort would
        // merely reproduce the round-robin order at O(N log N) per packet — pure
        // hot-path overhead. When scores diverge (Linux/macOS, or a failing conn on
        // any platform) the stable_sort orders best-first as before.
        if (!uniformScores)
        {
            std::stable_sort(order.begin(), order.end(),
                             [&](size_t a, size_t b) { return scores[a] > scores[b]; });
        }

        // Fallback: if backoff eliminated all candidates, allow any connected socket.
        bool anyEligible = (bestScore >= 0);

        bool sent = false;
        for (size_t rank = 0; rank < numDataConns; rank++)
        {
            size_t idx = order[rank];
            if (!anyEligible)
            {
                if (!connSnap[idx] || !connSnap[idx]->isConnected())
                    continue;
            }
            else if (scores[idx] < 0)
            {
                continue;
            }

            auto &conn = connSnap[idx];

            // No per-packet writability pre-poll. The socket is non-blocking, so
            // SendTcpDirect returns immediately with SOCKET_ERROR_WOULD_BLOCK when
            // the kernel send buffer is full. Dropping the IsSocketWritable() select()
            // removes one syscall per sent packet on the hot path; behavior is
            // unchanged because the WOULD_BLOCK/error case below already advances to
            // the next connection (and increments reenqueueCount).
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
                // Flush the rest of the packet on THIS connection — partial bytes are
                // already in the stream, so switching connections mid-packet would
                // corrupt the framing. A full kernel send buffer under load is
                // backpressure, not a dead socket: keep waiting for writability up to
                // PARTIAL_SEND_BUDGET_MS instead of giving up after one short poll.
                // Stalling here only delays throughput (and applies natural send-queue
                // backpressure); it does NOT create a receiver-side gap, because the
                // packets queued behind this one have not been sent yet. Only a genuine
                // socket error or a sustained stall past the budget tears the conn down.
                auto partialStart = std::chrono::steady_clock::now();
                bool hardError = false;
                while (totalSent < dataVec->size())
                {
                    if (!conn->isConnected() || !this->isRunning())
                        break;
                    if (!IsSocketWritable(conn->getSocketFd(), PARTIAL_SEND_POLL_MS))
                    {
                        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - partialStart)
                                             .count();
                        if (elapsedMs >= PARTIAL_SEND_BUDGET_MS)
                            break; // truly stuck — give up
                        continue;
                    }
                    ssize_t r = SendTcpDirect(
                        conn->getSocketFd(),
                        dataVec->data() + totalSent,
                        dataVec->size() - totalSent, 0);
                    if (r > 0)
                    {
                        totalSent += static_cast<size_t>(r);
                    }
                    else if (r == SOCKET_ERROR_WOULD_BLOCK || r == SOCKET_ERROR_INTERRUPTED)
                    {
                        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - partialStart)
                                             .count();
                        if (elapsedMs >= PARTIAL_SEND_BUDGET_MS)
                            break;
                        continue;
                    }
                    else
                    {
                        hardError = true;
                        break; // genuine connection error
                    }
                }

                if (totalSent < dataVec->size())
                {
                    // Couldn't finish — partial bytes already in the TCP stream make
                    // the framing unrecoverable, so this connection must be dropped.
                    // With the budget above this now only happens on a real error or a
                    // sustained (>= PARTIAL_SEND_BUDGET_MS) stall, not brief backpressure.
                    conn->disconnect();
                    if (idx < statsSnap.size() && statsSnap[idx])
                        statsSnap[idx]->reenqueueCount.fetch_add(1, std::memory_order_relaxed);
                    log_warnning("Partial send on conn " + std::to_string(idx) +
                                 " for msgId " + std::to_string(messageId) +
                                 " (" + std::to_string(totalSent) + "/" +
                                 std::to_string(dataVec->size()) + " bytes, " +
                                 (hardError ? "error" : "stalled") + "); disconnecting");
                    continue; // try next connection for this packet
                }

                if (messageTracker)
                    messageTracker->recordMessage(messageId, static_cast<int>(idx));

                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
                if (idx < statsSnap.size() && statsSnap[idx])
                {
                    auto &stats = statsSnap[idx];
                    stats->lastTxMessageId.store(messageId);
                    stats->lastTxTimeMs.store(nowMs);
                    stats->txCount.fetch_add(1);
                    stats->valid.store(true);
                    stats->reenqueueCount.store(0, std::memory_order_relaxed);
                }

                sent = true;
                break;
            }
            else
            {
                if (idx < statsSnap.size() && statsSnap[idx])
                    statsSnap[idx]->reenqueueCount.fetch_add(1, std::memory_order_relaxed);
                // WOULD_BLOCK just means this connection's buffer is full — skip to the
                // next one. A genuine error (SOCKET_ERROR_CLOSED) means the connection
                // is dead: disconnect it so the watchdog reaps and reconnects the slot.
                // Without this, a reset connection that never sees a read event would
                // linger as an undetected zombie that the scorer silently avoids.
                if (n == SOCKET_ERROR_CLOSED)
                    conn->disconnect();
            }
        }

        if (!sent)
        {
            // Retain the packet for the next iteration instead of re-enqueuing.
            // Block until the watchdog reconnects a slot (signalled via
            // connAvailableCv) or until the send/resend queues receive new
            // work. This avoids burning CPU while no connections are available
            // yet wakes instantly when one becomes usable.
            pendingDataVec = std::move(dataVec);
            std::unique_lock<std::mutex> lock(connectionsMutex);
            connAvailableCv.wait_for(lock, std::chrono::milliseconds(50), [&] {
                if (!this->isRunning())
                    return true;
                for (size_t i = 0; i < numDataConns; i++)
                {
                    if (connections[i] && connections[i]->isConnected())
                        return true;
                }
                return false;
            });
        }
    }

    log_info("TcpVCSendThread stopped");
}

void TcpVCSendThread::sendOnResendConn(std::shared_ptr<std::vector<char>> data,
                                       const std::vector<TcpConnectionSp>& conns)
{
    if (conns.size() <= VC_FIRST_RESEND_CONN_INDEX)
    {
        log_warnning("[RESEND] No resend connections available (connections.size()=" +
                     std::to_string(conns.size()) + ")");
        return;
    }

    const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(data->data());
    uint64_t messageId = packet->header.messageId;

    const size_t resendCount = conns.size() - VC_FIRST_RESEND_CONN_INDEX;

    int aliveCount = 0;
    int nullCount = 0;
    int disconnectedCount = 0;
    int sendFailCount = 0;

    for (size_t attempt = 0; attempt < resendCount; attempt++)
    {
        size_t idx = VC_FIRST_RESEND_CONN_INDEX +
                     (resendRoundRobin + attempt) % resendCount;
        const auto &conn = conns[idx];

        if (!conn)
        {
            nullCount++;
            continue;
        }
        if (!conn->isConnected())
        {
            disconnectedCount++;
            continue;
        }

        aliveCount++;

        ssize_t n = SendTcpDirect(conn->getSocketFd(), data->data(), data->size(), 0);

        if (n > 0)
        {
            size_t totalSent = static_cast<size_t>(n);
            // Same budgeted completion as the normal send path: wait out transient
            // backpressure instead of killing a healthy-but-busy resend connection.
            auto partialStart = std::chrono::steady_clock::now();
            while (totalSent < data->size())
            {
                if (!conn->isConnected() || !this->isRunning())
                    break;
                if (!IsSocketWritable(conn->getSocketFd(), PARTIAL_SEND_POLL_MS))
                {
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - partialStart)
                                         .count();
                    if (elapsedMs >= PARTIAL_SEND_BUDGET_MS)
                        break;
                    continue;
                }
                ssize_t r = SendTcpDirect(conn->getSocketFd(),
                                          data->data() + totalSent,
                                          data->size() - totalSent, 0);
                if (r > 0)
                {
                    totalSent += static_cast<size_t>(r);
                }
                else if (r == SOCKET_ERROR_WOULD_BLOCK || r == SOCKET_ERROR_INTERRUPTED)
                {
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - partialStart)
                                         .count();
                    if (elapsedMs >= PARTIAL_SEND_BUDGET_MS)
                        break;
                    continue;
                }
                else
                {
                    break; // genuine connection error
                }
            }

            if (totalSent < data->size())
            {
                conn->disconnect();
                log_warnning("[RESEND] Partial send on resend conn " + std::to_string(idx) +
                             " for msgId=" + std::to_string(messageId) + "; disconnecting");
                continue;
            }

            resendRoundRobin = (resendRoundRobin + 1) % resendCount;
            resendRetryCount.erase(messageId);
            log_info("[RESEND] Sent resend msgId=" + std::to_string(messageId) +
                     " on conn " + std::to_string(idx));
            return;
        }
        else
        {
            sendFailCount++;
            log_debug("[RESEND] SendTcpDirect failed on conn " + std::to_string(idx) +
                      " for msgId=" + std::to_string(messageId) +
                      " rc=" + std::to_string(n));
            // A genuine error means this resend connection is dead — reap it so the
            // watchdog reconnects the slot. WOULD_BLOCK is left alone (transient).
            if (n == SOCKET_ERROR_CLOSED)
                conn->disconnect();
        }
    }

    resendRoundRobin = (resendRoundRobin + 1) % resendCount;

    // Only count a retry when at least one connection was alive but the send
    // still failed. When ALL connections are down, re-enqueue without burning
    // a retry — the watchdog needs time (~500ms) to reconnect slots.
    uint8_t retryCount = resendRetryCount[messageId];
    bool countThisRetry = (aliveCount > 0);

    if ((!countThisRetry || retryCount < MAX_RESEND_RETRIES) && resendQueue)
    {
        if (countThisRetry)
            resendRetryCount[messageId] = retryCount + 1;
        resendQueue->enqueue(data);
        log_debug("[RESEND] Re-enqueued msgId=" + std::to_string(messageId) +
                  " retry=" + std::to_string(countThisRetry ? retryCount + 1 : retryCount) +
                  " (alive=" + std::to_string(aliveCount) +
                  " null=" + std::to_string(nullCount) +
                  " disconn=" + std::to_string(disconnectedCount) +
                  " sendFail=" + std::to_string(sendFailCount) + ")");
    }
    else
    {
        resendRetryCount.erase(messageId);
        log_warnning("[RESEND] All resend connections failed for msgId=" +
                     std::to_string(messageId) + " after " +
                     std::to_string(retryCount) + " retries, dropping" +
                     " (alive=" + std::to_string(aliveCount) +
                     " null=" + std::to_string(nullCount) +
                     " disconn=" + std::to_string(disconnectedCount) +
                     " sendFail=" + std::to_string(sendFailCount) + ")");
    }
}
