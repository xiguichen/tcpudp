#include "TcpVirtualChannel.h"
#include "Log.h"
#include "TcpVCReadThreadFactory.h"
#include "TcpVCWriteThreadFactory.h"
#include "VcProtocol.h"
#include <cstring>
#include <format>

void TcpVirtualChannel::open()
{

    // Capture a shared_ptr to 'this' so the TcpVirtualChannel object is kept
    // alive for the entire duration of either callback, even if the last external
    // shared_ptr is released (e.g. via VcManager::Remove) while the callback is
    // still executing on a read thread. Without this, ~TcpVirtualChannel would
    // run on the read-thread itself, causing std::terminate in ~StopableThread.
    auto selfGuard = shared_from_this();

    auto dataCallback = [selfGuard](const uint64_t messageId, std::shared_ptr<std::vector<char>> data) {
        selfGuard->processReceivedData(messageId, data);
    };

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

            // Cancel any waiting operations on all send queues
            for (auto &q : self->sendQueues) {
                if (q) {
                    q->cancelWait();
                }
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

    for (int i = 0; i < this->connections.size(); ++i)
    {
        auto readThread = TcpVCReadThreadFactory::createThread(this->connections[i]);
        // Set callbacks before starting the thread to avoid missing early events
        readThread->setDataCallback(dataCallback);
        readThread->setDisconnectCallback(disconnectCB);
        readThread->start();
        readThreads.emplace_back(readThread);
        auto writeThread = TcpVCWriteThreadFactory::createThread(sendQueues[i], this->connections[i]);
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
        auto messageId = this->lastSendMessageId.fetch_add(1);
        auto messageIdNetwork = messageId;
        log_debug(std::format("Sending message with ID: {}", messageId));

        // Build the packet directly into the shared vector — no malloc needed.
        auto totalPacketSize = sizeof(VCDataPacket) + size;
        auto dataVec = std::make_shared<std::vector<char>>(totalPacketSize);

        VCDataPacket *packet = reinterpret_cast<VCDataPacket *>(dataVec->data());
        packet->header.type = VcPacketType::DATA;
        packet->header.messageId = messageIdNetwork;
        packet->dataLength = static_cast<uint16_t>(size);
        std::memcpy(packet->data, data, size);

        // Enqueue to ALL connections so every connection sends every message.
        // The receiver drops duplicates via messageId — fastest connection wins.
        for (auto &q : sendQueues)
        {
            auto queueDepth = q->size();
            if (queueDepth > 100)
            {
                log_info(std::format("[PERF-DIAG] Send queue depth is {}, msgID: {}. "
                    "High queue depth may indicate send timeouts causing retry buildup or lack of backpressure.",
                    queueDepth, messageId));
            }
            q->enqueue(dataVec);
        }
    }
}
bool TcpVirtualChannel::isOpen() const
{
    return opened;
}
void TcpVirtualChannel::close()
{
    // Atomically mark as closed; if it was already false, nothing to do
    if (!opened.exchange(false))
    {
        return;
    }

    // Cancel any waiting operations on all send queues
    for (auto &q : this->sendQueues) {
        if (q) {
            q->cancelWait();
        }
    }

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

    opened = false;
}
void TcpVirtualChannel::processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data)
{
    log_debug(std::format("Processing received message with ID: {}", messageId));
    std::lock_guard<std::mutex> lock(receivedDataMutex);

    // Check if this is a duplicate message
    if (messageId < nextMessageId)
    {
        log_info(std::format("[PERF-DIAG] Duplicate message received: msgID={}, nextExpected={}. "
            "Duplicates may indicate retry storms from send-timeout re-enqueue.",
            messageId, nextMessageId.load()));
        return; // Duplicate message, ignore
    }

    // Add the received data to the map
    receivedDataMap[messageId] = data;

    // Detect head-of-line blocking: message arrived but can't be delivered yet
    if (messageId != nextMessageId)
    {
        auto gap = messageId - nextMessageId.load();
        log_info(std::format("[PERF-DIAG] Out-of-order message: received msgID={}, waiting for msgID={}. "
            "Gap={}, buffered msgs={}. Head-of-line blocking: delivery stalled until msgID {} arrives.",
            messageId, nextMessageId.load(), gap, receivedDataMap.size(), nextMessageId.load()));
    }

    // Process messages in order
    size_t deliveredCount = 0;
    std::map<uint64_t, std::shared_ptr<std::vector<char>>>::iterator it;
    while ((it = receivedDataMap.find(nextMessageId)) != receivedDataMap.end())
    {
        // Call the receive callback with the data
        if (receiveCallback)
        {
            receiveCallback(it->second->data(), it->second->size());
        }

        // Remove the processed message from the map
        receivedDataMap.erase(it);

        // Move to the next expected message ID
        nextMessageId.fetch_add(1);
        deliveredCount++;
    }

    if (deliveredCount > 1)
    {
        log_info(std::format("[PERF-DIAG] Burst delivery: {} buffered messages delivered at once. "
            "Remaining buffered={}. Burst indicates prior head-of-line blocking was resolved.",
            deliveredCount, receivedDataMap.size()));
    }
    if (!receivedDataMap.empty())
    {
        log_info(std::format("[PERF-DIAG] Still waiting: {} messages buffered, next expected msgID={}. "
            "Oldest buffered msgID={}. Gap={}.",
            receivedDataMap.size(), nextMessageId.load(),
            receivedDataMap.begin()->first,
            receivedDataMap.begin()->first - nextMessageId.load()));
    }
}

TcpVirtualChannel::TcpVirtualChannel(std::vector<SocketFd> fds)
{
    for (auto fd : fds)
    {
        connections.emplace_back(std::make_shared<TcpConnection>(fd));
        sendQueues.emplace_back(std::make_shared<BlockingQueue>());
    }
}


