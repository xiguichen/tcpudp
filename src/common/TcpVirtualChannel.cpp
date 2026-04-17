#include "TcpVirtualChannel.h"
#include "Log.h"
#include "TcpVCReadThreadFactory.h"
#include "TcpVCWriteThreadFactory.h"
#include "VcProtocol.h"
#include <chrono>
#include <cstring>
#include <format>
#include <string>

static constexpr auto REORDER_TIMEOUT_MS = std::chrono::milliseconds(500);
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

    deliveryRunning = true;
    deliveryThread = std::thread(&TcpVirtualChannel::deliveryThreadFunc, this);

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
        auto writeThread = TcpVCWriteThreadFactory::createThread(sendQueue, this->connections[i], i);
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

    // Always stop delivery thread (handles disconnect callback setting opened=false first)
    {
        std::lock_guard<std::mutex> lock(deliveryMutex);
        deliveryRunning = false;
    }
    deliveryCv.notify_one();
    if (deliveryThread.joinable())
    {
        deliveryThread.join();
    }
}
void TcpVirtualChannel::processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data, int sourceConnIndex)
{
    static constexpr auto RX_MUTEX_WAIT_WARN_MS = std::chrono::milliseconds(50);

    std::vector<DeliveryItem> itemsToDeliver;

    struct
    {
        bool fired = false;
        long long elapsedMs = 0;
        uint64_t missingStart = 0, missingEnd = 0, skipTo = 0;
        size_t mapSize = 0;
        int lastDelivConn = -1;
        uint64_t firstBuf = 0, lastBuf = 0;
        int firstBufConn = -1, lastBufConn = -1;
        int suspectConn = -1;
        uint64_t suspectLastRx = 0;
        long long suspectAgeMs = 0;
        std::vector<size_t> bufByConn;
        size_t bufUnknown = 0;
        struct RxEntry
        {
            bool valid = false;
            uint64_t msgId = 0;
            long long ageMs = 0;
        };
        std::vector<RxEntry> rxEntries;
    } rtDiag;

    {
        std::unique_lock<std::timed_mutex> lock(receivedDataMutex, std::defer_lock);
        if (!lock.try_lock_for(RX_MUTEX_WAIT_WARN_MS))
        {
            int pending = -1;
            if (sourceConnIndex >= 0 && static_cast<size_t>(sourceConnIndex) < connections.size() &&
                connections[sourceConnIndex])
            {
                pending = SocketBytesAvailable(connections[sourceConnIndex]->getSocketFd());
            }
            log_warnning(
                std::format("[VC] processReceivedData waiting on receivedDataMutex: conn={} messageId={} pendingBytes={}",
                            sourceConnIndex, messageId, pending));
            lock.lock();
        }

        if (lastRxMessageId.empty())
        {
            lastRxMessageId.resize(connections.size());
            lastRxTime.resize(connections.size());
            lastRxValid.assign(connections.size(), false);
        }

        if (sourceConnIndex >= 0 && static_cast<size_t>(sourceConnIndex) < lastRxMessageId.size())
        {
            lastRxMessageId[sourceConnIndex] = messageId;
            lastRxTime[sourceConnIndex] = std::chrono::steady_clock::now();
            lastRxValid[sourceConnIndex] = true;
        }

        if (messageId < nextMessageId)
        {
            return;
        }

        receivedDataMap[messageId] = ReceivedItem{data, sourceConnIndex};
        itemsToDeliver = drainReceivedDataMap();

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
                if (elapsed >= REORDER_TIMEOUT_MS)
                {
                    rtDiag.fired = true;

                    auto firstIt = receivedDataMap.begin();
                    auto lastIt = std::prev(receivedDataMap.end());
                    rtDiag.skipTo = firstIt->first;
                    rtDiag.missingStart = nextMessageId.load();
                    rtDiag.missingEnd = (rtDiag.skipTo > 0) ? (rtDiag.skipTo - 1) : 0;
                    rtDiag.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                    rtDiag.mapSize = receivedDataMap.size();
                    rtDiag.lastDelivConn = lastDeliveredConnIndex;
                    rtDiag.firstBuf = firstIt->first;
                    rtDiag.firstBufConn = firstIt->second.sourceConnIndex;
                    rtDiag.lastBuf = lastIt->first;
                    rtDiag.lastBufConn = lastIt->second.sourceConnIndex;

                    auto now = std::chrono::steady_clock::now();

                    for (size_t i = 0; i < lastRxValid.size(); ++i)
                    {
                        if (!lastRxValid[i] || lastRxMessageId[i] >= rtDiag.missingStart)
                            continue;
                        auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRxTime[i]).count();
                        if (rtDiag.suspectConn == -1 || lastRxMessageId[i] < rtDiag.suspectLastRx ||
                            (lastRxMessageId[i] == rtDiag.suspectLastRx && ageMs > rtDiag.suspectAgeMs))
                        {
                            rtDiag.suspectConn = static_cast<int>(i);
                            rtDiag.suspectLastRx = lastRxMessageId[i];
                            rtDiag.suspectAgeMs = ageMs;
                        }
                    }

                    rtDiag.bufByConn.assign(connections.size(), 0);
                    for (const auto &kv : receivedDataMap)
                    {
                        int connIdx = kv.second.sourceConnIndex;
                        if (connIdx >= 0 && static_cast<size_t>(connIdx) < rtDiag.bufByConn.size())
                            rtDiag.bufByConn[connIdx]++;
                        else
                            rtDiag.bufUnknown++;
                    }

                    rtDiag.rxEntries.resize(lastRxValid.size());
                    for (size_t i = 0; i < lastRxValid.size(); ++i)
                    {
                        rtDiag.rxEntries[i].valid = lastRxValid[i];
                        if (lastRxValid[i])
                        {
                            rtDiag.rxEntries[i].msgId = lastRxMessageId[i];
                            rtDiag.rxEntries[i].ageMs =
                                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRxTime[i]).count();
                        }
                    }

                    nextMessageId.store(rtDiag.skipTo);
                    gapTimerActive = false;
                    auto moreItems = drainReceivedDataMap();
                    itemsToDeliver.insert(itemsToDeliver.end(), std::make_move_iterator(moreItems.begin()),
                                          std::make_move_iterator(moreItems.end()));
                }
            }
        }
        else
        {
            gapTimerActive = false;
        }
    }
    // receivedDataMutex released — read threads return to socket immediately

    if (rtDiag.fired)
    {
        int suspectPendingBytes = -1;
        if (rtDiag.suspectConn >= 0 && static_cast<size_t>(rtDiag.suspectConn) < connections.size() &&
            connections[rtDiag.suspectConn])
        {
            suspectPendingBytes = SocketBytesAvailable(connections[rtDiag.suspectConn]->getSocketFd());
        }

        std::string sendDiag;
        if (rtDiag.suspectConn >= 0 && static_cast<size_t>(rtDiag.suspectConn) < connections.size() &&
            connections[rtDiag.suspectConn])
        {
            auto &conn = connections[rtDiag.suspectConn];
            if (conn->diagIsSendInFlight())
            {
                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
                sendDiag = std::format(", sendInFlight=true sendMsgId={} sendAgeMs={}", conn->diagInFlightMessageId(),
                                       nowMs - conn->diagInFlightStartMs());
            }
            else
            {
                sendDiag = std::format(", sendIdle lastSendMsgId={}", conn->diagLastSendCompletedMessageId());
            }
        }

        std::string bufferedSummary;
        bufferedSummary.reserve(96);
        bufferedSummary += "bufByConn=[";
        for (size_t i = 0; i < rtDiag.bufByConn.size(); ++i)
        {
            if (i > 0)
                bufferedSummary += ",";
            bufferedSummary += std::format("{}:{}", i, rtDiag.bufByConn[i]);
        }
        if (rtDiag.bufUnknown > 0)
            bufferedSummary += std::format(",u:{}", rtDiag.bufUnknown);
        bufferedSummary += "]";

        std::string rxSummary;
        rxSummary.reserve(128);
        rxSummary += "rx=[";
        for (size_t i = 0; i < rtDiag.rxEntries.size(); ++i)
        {
            if (i > 0)
                rxSummary += ",";
            if (!rtDiag.rxEntries[i].valid)
                rxSummary += std::format("{}:-", i);
            else
                rxSummary += std::format("{}:{}@{}ms", i, rtDiag.rxEntries[i].msgId, rtDiag.rxEntries[i].ageMs);
        }
        rxSummary += "]";

        log_warnning(std::format(
            "Reorder timeout ({}ms): skipping messageIds {}-{}, advancing to {}. Buffered={} "
            "(lastDeliveredConn={}, firstBuffered={} conn={}, lastBuffered={} conn={}, suspectConn={} "
            "suspectRx={}@{}ms pendingBytes={}{}, {}, {})",
            rtDiag.elapsedMs, rtDiag.missingStart, rtDiag.missingEnd, rtDiag.skipTo, rtDiag.mapSize,
            rtDiag.lastDelivConn, rtDiag.firstBuf, rtDiag.firstBufConn, rtDiag.lastBuf, rtDiag.lastBufConn,
            rtDiag.suspectConn, rtDiag.suspectConn == -1 ? 0ULL : rtDiag.suspectLastRx,
            rtDiag.suspectConn == -1 ? 0LL : rtDiag.suspectAgeMs, suspectPendingBytes, sendDiag, bufferedSummary,
            rxSummary));
    }

    if (!itemsToDeliver.empty())
    {
        {
            std::lock_guard<std::mutex> lock(deliveryMutex);
            for (auto &item : itemsToDeliver)
            {
                deliveryBuffer.push_back(std::move(item));
            }
        }
        deliveryCv.notify_one();
    }
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

void TcpVirtualChannel::deliveryThreadFunc()
{
    log_info("Delivery thread started");

    static constexpr auto DELIVERY_SLOW_WARN_MS = std::chrono::milliseconds(100);

    while (true)
    {
        std::vector<DeliveryItem> items;
        {
            std::unique_lock<std::mutex> lock(deliveryMutex);
            deliveryCv.wait(lock, [this] { return !deliveryBuffer.empty() || !deliveryRunning; });
            if (!deliveryRunning && deliveryBuffer.empty())
            {
                break;
            }
            items = std::move(deliveryBuffer);
        }

        auto deliveryStart = std::chrono::steady_clock::now();
        long long cbTotalMs = 0;

        for (auto &item : items)
        {
            if (receiveCallback)
            {
                auto cbStart = std::chrono::steady_clock::now();
                receiveCallback(item.data->data(), item.data->size());
                auto cbDur = std::chrono::steady_clock::now() - cbStart;
                cbTotalMs += std::chrono::duration_cast<std::chrono::milliseconds>(cbDur).count();
                if (cbDur >= RECEIVE_CALLBACK_SLOW_WARN_MS)
                {
                    auto cbMs = std::chrono::duration_cast<std::chrono::milliseconds>(cbDur).count();
                    log_warnning(std::format("[VC] Slow receiveCallback: messageId={} conn={} bytes={} cbMs={}",
                                             item.messageId, item.sourceConnIndex, item.data->size(), cbMs));
                }
            }
        }

        auto deliveryDur = std::chrono::steady_clock::now() - deliveryStart;
        if (deliveryDur >= DELIVERY_SLOW_WARN_MS)
        {
            auto deliveryMs = std::chrono::duration_cast<std::chrono::milliseconds>(deliveryDur).count();
            log_warnning(std::format("[VC] delivery slow: delivered={} deliveryMs={} cbTotalMs={}",
                                     items.size(), deliveryMs, cbTotalMs));
        }
    }

    log_info("Delivery thread stopped");
}

TcpVirtualChannel::TcpVirtualChannel(std::vector<SocketFd> fds)
{
    for (auto fd : fds)
    {
        connections.emplace_back(std::make_shared<TcpConnection>(fd));
    }
    sendQueue = std::make_shared<BlockingQueue>();
}
