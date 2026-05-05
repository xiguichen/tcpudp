#include "TcpVCWriteThread.h"
#include "Log.h"
#include "Socket.h"
#include "VcProtocol.h"
#include <chrono>
#include <format>
#include <thread>

static constexpr int TCP_RUNTIME_REFRESH_MS = 250;
static constexpr auto SOCKET_AVOIDANCE_BASE_MS = std::chrono::milliseconds(2000);

void TcpVCWriteThread::run()
{
    log_info("TcpVCWriteThread started");

    static constexpr auto SLOW_SEND_WARN_MS = std::chrono::milliseconds(100);
    static constexpr int WRITABLE_CHECK_TIMEOUT_MS = 50;

    while (this->isRunning())
    {
        auto data = writeQueue->dequeue();
        if (!data)
        {
            log_info("Thread exiting on null data (queue cancelled or stopped)");
            break;
        }
        if (!this->isRunning())
        {
            log_info("Thread exiting on stop signal");
            break;
        }
        if (this->connection && this->connection->isConnected())
        {
            uint64_t messageId = 0;
            uint16_t payloadLen = 0;
            if (data->size() >= sizeof(VCDataPacket))
            {
                const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(data->data());
                messageId = packet->header.messageId;
                payloadLen = packet->dataLength;
            }

            if (socketStatus && socketStatus->isCurrentlyDegraded(SOCKET_AVOIDANCE_BASE_MS))
            {
                log_debug(std::format("[VC] WriteThread conn={}: socket degraded, re-enqueueing messageId={}",
                                      connectionIndex, messageId));
                if (sendStats)
                    sendStats->reenqueueCount.fetch_add(1, std::memory_order_relaxed);
                writeQueue->enqueue(data);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (!IsSocketWritable(connection->getSocketFd(), WRITABLE_CHECK_TIMEOUT_MS))
            {
                connection->refreshRuntimeInfoIfStale(std::chrono::milliseconds{TCP_RUNTIME_REFRESH_MS});
                auto runtimeInfo = connection->getLastRuntimeInfo();
                log_warnning(
                    std::format("[VC] WriteThread conn={}: socket not writable after {}ms, re-enqueueing messageId={}",
                                connectionIndex, WRITABLE_CHECK_TIMEOUT_MS, messageId));
                if (sendStats)
                    sendStats->reenqueueCount.fetch_add(1, std::memory_order_relaxed);
                writeQueue->enqueue(data);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            connection->refreshRuntimeInfoIfStale(std::chrono::milliseconds{TCP_RUNTIME_REFRESH_MS});
            auto runtimeInfo = connection->getLastRuntimeInfo();

            if (runtimeInfo.isCongested || runtimeInfo.isInExponentialBackoff)
            {
                log_warnning(
                    std::format("[VC] WriteThread conn={}: socket congested or in backoff, re-enqueueing messageId={} "
                                "(congested={}, backoff={})",
                                connectionIndex, messageId, runtimeInfo.isCongested ? "yes" : "no",
                                runtimeInfo.isInExponentialBackoff ? "yes" : "no"));

                if (sendStats)
                    sendStats->reenqueueCount.fetch_add(1, std::memory_order_relaxed);

                if (socketStatus)
                    socketStatus->markDegraded();

                writeQueue->enqueue(data);

                if (runtimeInfo.isInExponentialBackoff)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));

                continue;
            }

            auto start = std::chrono::steady_clock::now();
            connection->diagMarkSendStart(messageId);
            connection->send(data->data(), data->size());
            connection->diagMarkSendEnd(messageId);
            auto dur = std::chrono::steady_clock::now() - start;

            if (messageTracker)
            {
                messageTracker->recordMessage(messageId, connectionIndex);
            }

            if (sendStats)
            {
                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
                sendStats->lastTxMessageId.store(messageId, std::memory_order_relaxed);
                sendStats->lastTxTimeMs.store(nowMs, std::memory_order_relaxed);
                sendStats->txCount.fetch_add(1, std::memory_order_relaxed);
                sendStats->valid.store(true, std::memory_order_release);
                if (dur >= SLOW_SEND_WARN_MS)
                    sendStats->slowSendCount.fetch_add(1, std::memory_order_relaxed);
            }

            connection->refreshRuntimeInfoIfStale(std::chrono::milliseconds{TCP_RUNTIME_REFRESH_MS});

            if (dur >= SLOW_SEND_WARN_MS)
            {
                auto durMs = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
                auto runtimeInfo = connection->sampleRuntimeInfo();
                log_warnning(std::format("[VC] Slow TCP send: conn={} messageId={} payload={}B total={}B sendMs={}",
                                         connectionIndex, messageId, payloadLen, data->size(), durMs));
            }
        }
    }

    log_info("TcpVCWriteThread stopped");
}
TcpVCWriteThread::~TcpVCWriteThread()
{
    log_info("~TcpVCWriteThread");
}
