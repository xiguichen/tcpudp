#pragma once

#include "BlockingQueue.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

struct ConnSendStats
{
    std::atomic<uint64_t> lastTxMessageId{0};
    std::atomic<int64_t> lastTxTimeMs{0};
    std::atomic<uint64_t> txCount{0};
    std::atomic<uint64_t> reenqueueCount{0};
    std::atomic<uint64_t> slowSendCount{0};
    std::atomic<bool> valid{false};
};

class MessageTracker
{
  public:
    void recordMessage(uint64_t messageId, int connectionIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        messageToConn[messageId] = connectionIndex;
    }

    int getConnectionIndex(uint64_t messageId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = messageToConn.find(messageId);
        if (it != messageToConn.end())
        {
            return it->second;
        }
        return -1;
    }

    void removeMessage(uint64_t messageId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        messageToConn.erase(messageId);
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        messageToConn.clear();
    }

  private:
    std::mutex mutex;
    std::unordered_map<uint64_t, int> messageToConn;
};

struct SocketStatus
{
    int connectionIndex{-1};
    std::atomic<bool> isDegraded{false};
    std::chrono::steady_clock::time_point degradedSince;
    std::atomic<int> degradationCount{0};

    void markDegraded()
    {
        isDegraded.store(true);
        degradedSince = std::chrono::steady_clock::now();
        degradationCount.fetch_add(1);
    }

    bool isCurrentlyDegraded(std::chrono::milliseconds maxDuration) const
    {
        if (!isDegraded.load())
        {
            return false;
        }
        auto elapsed = std::chrono::steady_clock::now() - degradedSince;
        int64_t baseMs = maxDuration.count();
        int count = degradationCount.load();
        int64_t effectiveMs = baseMs * (1LL << std::min(count - 1, 3));
        return elapsed < std::chrono::milliseconds(effectiveMs);
    }

    void recover()
    {
        isDegraded.store(false);
        degradationCount.store(0);
    }
};

class TcpVCWriteThread : public StopableThread
{
  public:
    TcpVCWriteThread(BlockingQueueSp writeQueue, TcpConnectionSp connection, int connectionIndex,
                     std::shared_ptr<ConnSendStats> sendStats,
                     std::shared_ptr<MessageTracker> messageTracker,
                     std::shared_ptr<SocketStatus> socketStatus)
        : writeQueue(writeQueue), connection(connection), connectionIndex(connectionIndex),
          sendStats(sendStats), messageTracker(messageTracker), socketStatus(socketStatus)
    {
    }
    virtual ~TcpVCWriteThread();

  protected:
    virtual void run();

  private:
    BlockingQueueSp writeQueue;
    TcpConnectionSp connection;
    int connectionIndex{-1};
    std::shared_ptr<ConnSendStats> sendStats;
    std::shared_ptr<MessageTracker> messageTracker;
    std::shared_ptr<SocketStatus> socketStatus;
};

typedef std::shared_ptr<TcpVCWriteThread> TcpVCWriteThreadSp;
