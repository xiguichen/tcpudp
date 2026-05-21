#pragma once

#include "BlockingQueue.h"
#include "Socket.h"
#include "SpscQueue.h"
#include "TcpVCIoThread.h"
#include "TcpVCSendThread.h"
#include "TcpVCWriteThread.h"
#include "VirtualChannel.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

class SentDataCache
{
    struct ConnShard
    {
        std::mutex mutex;
        std::unordered_map<uint64_t, std::shared_ptr<std::vector<char>>> items;
        std::deque<uint64_t> insertionOrder; // deque for O(1) front removal on eviction
    };

  public:
    SentDataCache() = default;
    explicit SentDataCache(size_t numConns, size_t capacityPerConn)
        : shards(numConns), capacityPerConn(capacityPerConn) {}

    void insert(uint64_t messageId, std::shared_ptr<std::vector<char>> data);
    std::shared_ptr<std::vector<char>> find(uint64_t messageId);
    void clear();

  private:
    std::vector<ConnShard> shards;
    size_t capacityPerConn = 128;
};

class TcpVirtualChannel : public VirtualChannel, public std::enable_shared_from_this<TcpVirtualChannel>
{

  public:
    TcpVirtualChannel(std::vector<SocketFd> fds);
    virtual ~TcpVirtualChannel()
    {
        this->close();
    };

    virtual void open();

    virtual void send(const char *data, size_t size);

    virtual bool isOpen() const;

    virtual void close();

    void processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data, int sourceConnIndex);

    void processResendRequest(uint64_t messageId);

    void processMissingNotify(const std::vector<uint64_t> &missingIds);

    void setReorderTimeout(std::chrono::milliseconds timeout) { reorderTimeoutMs = timeout; }

    void setMissingNotifyInterval(std::chrono::milliseconds timeout) { missingNotifyIntervalMs = timeout; }

    void setResendCallback(std::function<void(uint64_t messageId, const char *data, size_t size)> callback)
    {
        resendCallback = std::move(callback);
    }

    void setMissingNotifyCallback(std::function<void(const std::vector<uint64_t> &missingIds)> callback)
    {
        missingNotifyCallback = std::move(callback);
    }

    std::shared_ptr<MessageTracker> getMessageTracker() const { return messageTracker; }

    std::vector<std::shared_ptr<SocketStatus>> getSocketStatuses() const { return socketStatuses; }

  private:
    struct ReceivedItem
    {
        std::shared_ptr<std::vector<char>> data;
        int sourceConnIndex{-1};
    };

    struct DeliveryItem
    {
        uint64_t messageId{0};
        std::shared_ptr<std::vector<char>> data;
        int sourceConnIndex{-1};
    };

    std::vector<DeliveryItem> drainReceivedDataMap();

    void sendMissingNotifications();

    std::shared_ptr<TcpVCIoThread> ioThread;
    std::shared_ptr<TcpVCSendThread> sendThread;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    BlockingQueueSp resendQueue; // dedicated queue for resend traffic (conn VC_RESEND_CONN_INDEX)

    std::vector<std::shared_ptr<ConnSendStats>> connSendStats;

    std::vector<std::unique_ptr<SpscQueue<DeliveryItem>>> connReceiveQueues;
    std::atomic<uint64_t> reorderEnqueueSeq{0};
    std::mutex reorderMutex;
    std::condition_variable reorderCv;

    std::map<uint64_t, ReceivedItem> receivedDataMap;

    std::mutex disconnectMutex;

    std::thread reorderThread;
    std::atomic<bool> reorderRunning{false};
    void reorderThreadFunc();

    std::atomic<bool> opened{false};
    std::atomic<uint64_t> lastSendMessageId{0}; // uint64_t: long is 32-bit on Windows (MSVC)
    std::atomic<uint64_t> nextMessageId{0};
    std::chrono::steady_clock::time_point gapFirstSeen;
    bool gapTimerActive{false};
    int lastDeliveredConnIndex{-1};
    std::chrono::steady_clock::time_point lastHealthLogTime;
    std::chrono::milliseconds reorderTimeoutMs{4000};

    std::vector<uint64_t> lastRxMessageId;
    std::vector<std::chrono::steady_clock::time_point> lastRxTime;
    std::vector<bool> lastRxValid;

    std::shared_ptr<MessageTracker> messageTracker;
    std::vector<std::shared_ptr<SocketStatus>> socketStatuses;

    std::unordered_set<uint64_t> notifiedMissingIds;
    std::chrono::steady_clock::time_point lastNotifyTime;
    std::chrono::milliseconds missingNotifyIntervalMs{200};

    SentDataCache sentDataCache;

    std::function<void(uint64_t messageId, const char *data, size_t size)> resendCallback;
    std::function<void(const std::vector<uint64_t> &missingIds)> missingNotifyCallback;
};
