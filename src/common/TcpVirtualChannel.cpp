#include "TcpVirtualChannel.h"
#include "Log.h"
#include "TcpVCReadThreadFactory.h"
#include "TcpVCWriteThreadFactory.h"
#include "VcProtocol.h"
#include <chrono>
#include <cstring>
#include <format>
#include <string>

static constexpr auto RECEIVE_CALLBACK_SLOW_WARN_MS = std::chrono::milliseconds(50);

void TcpVirtualChannel::open()
{

    // Capture a shared_ptr to 'this' so the TcpVirtualChannel object is kept
    // alive for the entire duration of either callback, even if the last external
    // shared_ptr is released (e.g. via VcManager::Remove) while the callback is
    // still executing on a read thread. Without this, ~TcpVirtualChannel would
    // run on the read-thread itself, causing std::terminate in ~StopableThread.
    auto selfGuard = shared_from_this();

    auto disconnectCB = [selfGuard](TcpConnectionSp /*unused*/) {
        auto *self = selfGuard.get();
        // Use a flag to track if we should notify the upper layer
        bool shouldNotify = false;

        {
            // Ensure only one disconnect routine runs at a time
            std::lock_guard<std::mutex> lock(self->disconnectMutex);

            // Early exit if already closed to prevent redundant work
            if (!self->opened) {
                log_debug("Disconnect callback called but VC already closed, skipping.");
                return;
            }

            log_debug("Disconnect detected, closing the entire virtual channel.");

            // Mark the virtual channel as closed to prevent further sends
            self->opened = false;
            shouldNotify = (self->disconnectCallback != nullptr);

            // Cancel any waiting operations on the send queue
            if (self->sendQueue) {
                self->sendQueue->cancelWait();
            }

            // Close all the connections first
            for (auto &conn : self->connections) {
                if (conn && conn->isConnected()) {
                    conn->disconnect();
                }
            }

            // Set running to false for all threads without joining
            // This allows threads to exit gracefully without deadlock
            for (auto &thread : self->readThreads) {
                if (thread) {
                    thread->setRunning(false);
                }
            }
            for (auto &thread : self->writeThreads) {
                if (thread) {
                    thread->setRunning(false);
                }
            }
        }
        // Mutex released here - now other threads can proceed and exit

        // Notify upper layer outside the lock to avoid potential deadlocks
        if (shouldNotify && self->disconnectCallback) {
            self->disconnectCallback();
        }
    };

    // Initialize per-connection receive stats
    lastRxMessageId.resize(connections.size(), 0);
    lastRxTime.resize(connections.size());
    lastRxValid.assign(connections.size(), false);

    // Create per-connection lock-free SPSC queues
    connReceiveQueues.clear();
    for (size_t i = 0; i < connections.size(); ++i)
    {
        connReceiveQueues.push_back(std::make_unique<SpscQueue<DeliveryItem>>());
    }

    // Create per-connection send stats
    connSendStats.clear();
    for (size_t i = 0; i < connections.size(); ++i)
    {
        connSendStats.push_back(std::make_shared<ConnSendStats>());
    }

    reorderRunning = true;
    reorderThread = std::thread(&TcpVirtualChannel::reorderThreadFunc, this);

    for (int i = 0; i < this->connections.size(); ++i)
    {
        auto readThread = TcpVCReadThreadFactory::createThread(this->connections[i], i);
        auto dataCallback = [selfGuard, i](const uint64_t messageId, std::shared_ptr<std::vector<char>> data) {
            selfGuard->processReceivedData(messageId, data, i);
        };
        // Set callbacks before starting the thread to avoid missing early events
        readThread->setDataCallback(dataCallback);
        readThread->setDisconnectCallback(disconnectCB);
        readThread->start();
        readThreads.emplace_back(readThread);
        auto writeThread = TcpVCWriteThreadFactory::createThread(sendQueue, this->connections[i], i, connSendStats[i]);
        writeThread->start();
        writeThreads.emplace_back(writeThread);
    }
    opened = true;
}

void TcpVirtualChannel::send(const char *data, size_t size)
{
    if (!opened)
    {
        log_debug("send called on closed channel, ignoring");
        return;
    }
    if (data != nullptr && size > 0)
    {
        if (size > VC_MAX_DATA_PAYLOAD_SIZE)
        {
            log_error(std::format("UDP packet size {} exceeds VC_MAX_DATA_PAYLOAD_SIZE {}, dropping", size, VC_MAX_DATA_PAYLOAD_SIZE));
            return;
        }

        auto quesize = this->sendQueue->size();
        if (quesize > SEND_QUEUE_DROP_THRESHOLD)
        {
            log_info(std::format("[PERF-DIAG] Send queue depth {} exceeds threshold {}. "
                "Dropping UDP packet to apply backpressure.",
                quesize, SEND_QUEUE_DROP_THRESHOLD));
            return;
        }

        auto messageId = this->lastSendMessageId.fetch_add(1);
        auto messageIdNetwork = messageId;

        // Build the packet directly into the shared vector — no malloc needed.
        auto totalPacketSize = sizeof(VCDataPacket) + size;
        auto dataVec = std::make_shared<std::vector<char>>(totalPacketSize);

        VCDataPacket *packet = reinterpret_cast<VCDataPacket *>(dataVec->data());
        packet->header.type = VcPacketType::DATA;
        packet->header.messageId = messageIdNetwork;
        packet->dataLength = static_cast<uint16_t>(size);
        std::memcpy(packet->data, data, size);
        sendQueue->enqueue(dataVec);

    }
}
bool TcpVirtualChannel::isOpen() const
{
    return opened;
}
void TcpVirtualChannel::close()
{
    bool wasOpen = opened.exchange(false);

    if (wasOpen)
    {
        // Cancel any waiting operations
        this->sendQueue->cancelWait();

        // Close all the connections and stop threads
        log_debug("Closing TcpVirtualChannel connections");
        for (auto &conn : connections)
        {
            if (conn && conn->isConnected())
            {
                log_debug("Disconnecting a TcpConnection");
                conn->disconnect();
            }
            else
            {
                log_debug("TcpConnection already disconnected");
            }
        }

        log_debug("Closing TcpVirtualChannel");

        // Stop and join all read threads
        log_debug("Stopping and joining read threads");
        for (auto &thread : readThreads)
        {
            if (thread)
            {
                log_debug("Stopping a read thread");
                thread->stop();
                log_debug("Joining a read thread");
                thread->joinThread();
                log_debug("Read thread stopped and joined");
            }
        }

        log_debug("All read threads stopped and joined");

        // Stop and join all write threads
        log_debug("Stopping and joining write threads");
        for (auto &thread : writeThreads)
        {
            if (thread)
            {
                thread->stop();
                thread->joinThread();
            }
        }
        log_debug("All write threads stopped and joined");
    }

    // Always stop reorder thread
    reorderRunning.store(false);
    reorderCv.notify_one();
    if (reorderThread.joinable())
    {
        reorderThread.join();
    }
}

void TcpVirtualChannel::processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data,
                                             int sourceConnIndex)
{
    if (sourceConnIndex >= 0 && static_cast<size_t>(sourceConnIndex) < connReceiveQueues.size())
    {
        bool enqueued = connReceiveQueues[sourceConnIndex]->try_enqueue({messageId, data, sourceConnIndex});
        if (!enqueued)
        {
            log_warnning(
                std::format("[VC] connReceiveQueue[{}] full, dropping messageId={}", sourceConnIndex, messageId));
            return;
        }
        reorderEnqueueSeq.fetch_add(1, std::memory_order_release);
    }
    reorderCv.notify_one();
}

std::vector<TcpVirtualChannel::DeliveryItem> TcpVirtualChannel::drainReceivedDataMap()
{
    std::vector<DeliveryItem> items;

    std::map<uint64_t, ReceivedItem>::iterator it;
    while ((it = receivedDataMap.find(nextMessageId)) != receivedDataMap.end())
    {
        lastDeliveredConnIndex = it->second.sourceConnIndex;
        items.push_back({it->first, std::move(it->second.data), it->second.sourceConnIndex});
        receivedDataMap.erase(it);
        nextMessageId.fetch_add(1);
    }

    return items;
}

void TcpVirtualChannel::reorderThreadFunc()
{
    log_info("Reorder thread started");

    uint64_t lastProcessedSeq = 0;

    while (reorderRunning.load())
    {
        bool gotAny = false;

        // Drain all per-connection lock-free SPSC queues (zero contention)
        for (size_t i = 0; i < connReceiveQueues.size(); i++)
        {
            DeliveryItem item;
            while (connReceiveQueues[i]->try_dequeue(item))
            {
                if (item.sourceConnIndex >= 0 &&
                    static_cast<size_t>(item.sourceConnIndex) < lastRxMessageId.size())
                {
                    lastRxMessageId[item.sourceConnIndex] = item.messageId;
                    lastRxTime[item.sourceConnIndex] = std::chrono::steady_clock::now();
                    lastRxValid[item.sourceConnIndex] = true;
                }

                if (item.messageId < nextMessageId)
                    continue;

                receivedDataMap.try_emplace(item.messageId, ReceivedItem{item.data, item.sourceConnIndex});
                gotAny = true;
            }
        }
        lastProcessedSeq = reorderEnqueueSeq.load(std::memory_order_acquire);

        // Drain consecutive messages
        auto itemsToDeliver = drainReceivedDataMap();

        // Check reorder timeout (no lock needed — single-thread access)
        if (!receivedDataMap.empty())
        {
            if (!gapTimerActive)
            {
                gapFirstSeen = std::chrono::steady_clock::now();
                gapTimerActive = true;
            }
            else
            {
                auto elapsed = std::chrono::steady_clock::now() - gapFirstSeen;
                if (elapsed >= reorderTimeoutMs)
                {
                    auto firstIt = receivedDataMap.begin();
                    auto lastIt = std::prev(receivedDataMap.end());
                    auto skipTo = firstIt->first;
                    auto missingStart = nextMessageId.load();
                    auto missingEnd = (skipTo > 0) ? (skipTo - 1) : 0ULL;
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                    auto mapSize = receivedDataMap.size();
                    auto lastDelivConn = lastDeliveredConnIndex;
                    auto firstBuf = firstIt->first;
                    auto firstBufConn = firstIt->second.sourceConnIndex;
                    auto lastBuf = lastIt->first;
                    auto lastBufConn = lastIt->second.sourceConnIndex;

                    auto now = std::chrono::steady_clock::now();

                    int suspectConn = -1;
                    uint64_t suspectLastRx = 0;
                    long long suspectAgeMs = 0;
                    for (size_t i = 0; i < lastRxValid.size(); ++i)
                    {
                        if (!lastRxValid[i] || lastRxMessageId[i] >= missingStart)
                            continue;
                        auto ageMs =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRxTime[i]).count();
                        if (suspectConn == -1 || lastRxMessageId[i] < suspectLastRx ||
                            (lastRxMessageId[i] == suspectLastRx && ageMs > suspectAgeMs))
                        {
                            suspectConn = static_cast<int>(i);
                            suspectLastRx = lastRxMessageId[i];
                            suspectAgeMs = ageMs;
                        }
                    }

                    std::vector<size_t> bufByConn(connections.size(), 0);
                    size_t bufUnknown = 0;
                    for (const auto &kv : receivedDataMap)
                    {
                        int connIdx = kv.second.sourceConnIndex;
                        if (connIdx >= 0 && static_cast<size_t>(connIdx) < bufByConn.size())
                            bufByConn[connIdx]++;
                        else
                            bufUnknown++;
                    }

                    nextMessageId.store(skipTo);
                    gapTimerActive = false;
                    auto moreItems = drainReceivedDataMap();
                    itemsToDeliver.insert(itemsToDeliver.end(), std::make_move_iterator(moreItems.begin()),
                                          std::make_move_iterator(moreItems.end()));

                    // Diagnostics outside the hot path
                    int suspectPendingBytes = -1;
                    if (suspectConn >= 0 && static_cast<size_t>(suspectConn) < connections.size() &&
                        connections[suspectConn])
                    {
                        suspectPendingBytes = SocketBytesAvailable(connections[suspectConn]->getSocketFd());
                    }

                    std::string sendDiag;
                    if (suspectConn >= 0 && static_cast<size_t>(suspectConn) < connections.size() &&
                        connections[suspectConn])
                    {
                        auto &conn = connections[suspectConn];
                        if (conn->diagIsSendInFlight())
                        {
                            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count();
                            sendDiag = std::format(", sendInFlight=true sendMsgId={} sendAgeMs={}",
                                                   conn->diagInFlightMessageId(),
                                                   nowMs - conn->diagInFlightStartMs());
                        }
                        else
                        {
                            sendDiag =
                                std::format(", sendIdle lastSendMsgId={}", conn->diagLastSendCompletedMessageId());
                        }
                    }

                    std::string bufferedSummary;
                    bufferedSummary.reserve(96);
                    bufferedSummary += "bufByConn=[";
                    for (size_t i = 0; i < bufByConn.size(); ++i)
                    {
                        if (i > 0)
                            bufferedSummary += ",";
                        bufferedSummary += std::format("{}:{}", i, bufByConn[i]);
                    }
                    if (bufUnknown > 0)
                        bufferedSummary += std::format(",u:{}", bufUnknown);
                    bufferedSummary += "]";

                    std::string rxSummary;
                    rxSummary.reserve(128);
                    rxSummary += "rx=[";
                    for (size_t i = 0; i < lastRxValid.size(); ++i)
                    {
                        if (i > 0)
                            rxSummary += ",";
                        if (!lastRxValid[i])
                            rxSummary += std::format("{}:-", i);
                        else
                        {
                            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRxTime[i]).count();
                            rxSummary += std::format("{}:{}@{}ms", i, lastRxMessageId[i], ageMs);
                        }
                    }
                    rxSummary += "]";

                    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now.time_since_epoch())
                                     .count();
                    std::string txSummary;
                    txSummary.reserve(192);
                    txSummary += "tx=[";
                    for (size_t i = 0; i < connSendStats.size(); ++i)
                    {
                        if (i > 0)
                            txSummary += ",";
                        auto &s = connSendStats[i];
                        if (!s->valid.load(std::memory_order_acquire))
                            txSummary += std::format("{}:-", i);
                        else
                        {
                            auto txAge = nowMs - s->lastTxTimeMs.load(std::memory_order_relaxed);
                            txSummary += std::format("{}:{}@{}ms/n{}/r{}/s{}",
                                                     i,
                                                     s->lastTxMessageId.load(std::memory_order_relaxed),
                                                     txAge,
                                                     s->txCount.load(std::memory_order_relaxed),
                                                     s->reenqueueCount.load(std::memory_order_relaxed),
                                                     s->slowSendCount.load(std::memory_order_relaxed));
                        }
                    }
                    txSummary += "]";

                    log_warnning(std::format(
                        "Reorder timeout ({}ms): skipping messageIds {}-{}, advancing to {}. Buffered={} "
                        "(lastDeliveredConn={}, firstBuffered={} conn={}, lastBuffered={} conn={}, suspectConn={} "
                        "suspectRx={}@{}ms pendingBytes={}{}, {}, {}, {})",
                        elapsedMs, missingStart, missingEnd, skipTo, mapSize, lastDelivConn, firstBuf, firstBufConn,
                        lastBuf, lastBufConn, suspectConn, suspectConn == -1 ? 0ULL : suspectLastRx,
                        suspectConn == -1 ? 0LL : suspectAgeMs, suspectPendingBytes, sendDiag, bufferedSummary,
                        rxSummary, txSummary));
                }
            }
        }
        else
        {
            gapTimerActive = false;
        }

        // Deliver messages via callback (single thread — no lock needed)
        for (auto &item : itemsToDeliver)
        {
            if (receiveCallback)
            {
                auto cbStart = std::chrono::steady_clock::now();
                receiveCallback(item.data->data(), item.data->size());
                auto cbDur = std::chrono::steady_clock::now() - cbStart;
                if (cbDur >= RECEIVE_CALLBACK_SLOW_WARN_MS)
                {
                    auto cbMs = std::chrono::duration_cast<std::chrono::milliseconds>(cbDur).count();
                    log_warnning(std::format("[VC] Slow receiveCallback: messageId={} conn={} bytes={} cbMs={}",
                                             item.messageId, item.sourceConnIndex, item.data->size(), cbMs));
                }
            }
        }

        // Periodic health log (every 10 seconds)
        {
            static constexpr auto HEALTH_LOG_INTERVAL = std::chrono::seconds(10);
            auto now = std::chrono::steady_clock::now();
            if (lastHealthLogTime.time_since_epoch().count() == 0)
                lastHealthLogTime = now;
            if (now - lastHealthLogTime >= HEALTH_LOG_INTERVAL)
            {
                lastHealthLogTime = now;
                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now.time_since_epoch())
                                 .count();

                std::string rxInfo;
                rxInfo.reserve(128);
                rxInfo += "rx=[";
                for (size_t i = 0; i < lastRxValid.size(); ++i)
                {
                    if (i > 0)
                        rxInfo += ",";
                    if (!lastRxValid[i])
                        rxInfo += std::format("{}:-", i);
                    else
                    {
                        auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRxTime[i]).count();
                        rxInfo += std::format("{}:{}@{}ms", i, lastRxMessageId[i], ageMs);
                    }
                }
                rxInfo += "]";

                std::string txInfo;
                txInfo.reserve(192);
                txInfo += "tx=[";
                for (size_t i = 0; i < connSendStats.size(); ++i)
                {
                    if (i > 0)
                        txInfo += ",";
                    auto &s = connSendStats[i];
                    if (!s->valid.load(std::memory_order_acquire))
                        txInfo += std::format("{}:-", i);
                    else
                    {
                        auto txAge = nowMs - s->lastTxTimeMs.load(std::memory_order_relaxed);
                        txInfo += std::format("{}:{}@{}ms/n{}/r{}/s{}",
                                              i,
                                              s->lastTxMessageId.load(std::memory_order_relaxed),
                                              txAge,
                                              s->txCount.load(std::memory_order_relaxed),
                                              s->reenqueueCount.load(std::memory_order_relaxed),
                                              s->slowSendCount.load(std::memory_order_relaxed));
                    }
                }
                txInfo += "]";

                log_info(std::format("[VC] Health: nextMsgId={} buffered={} queueDepth={} gap={}, {}, {}",
                                     nextMessageId.load(),
                                     receivedDataMap.size(),
                                     sendQueue->size(),
                                     gapTimerActive ? "active" : "none",
                                     rxInfo,
                                     txInfo));
            }
        }

        // Wait for new data or gap timeout
        if (!gotAny && itemsToDeliver.empty())
        {
            std::unique_lock<std::mutex> lock(reorderMutex);
            if (gapTimerActive)
            {
                auto remaining = reorderTimeoutMs - (std::chrono::steady_clock::now() - gapFirstSeen);
                if (remaining > std::chrono::milliseconds::zero())
                {
                    reorderCv.wait_for(lock, remaining, [&] {
                        return reorderEnqueueSeq.load(std::memory_order_acquire) > lastProcessedSeq ||
                               !reorderRunning;
                    });
                }
            }
            else
            {
                reorderCv.wait(lock, [&] {
                    return reorderEnqueueSeq.load(std::memory_order_acquire) > lastProcessedSeq || !reorderRunning;
                });
            }
        }
    }

    log_info("Reorder thread stopped");
}

TcpVirtualChannel::TcpVirtualChannel(std::vector<SocketFd> fds)
{
    for (auto fd : fds)
    {
        connections.emplace_back(std::make_shared<TcpConnection>(fd));
    }
    sendQueue = std::make_shared<BlockingQueue>();
}
