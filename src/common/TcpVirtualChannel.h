#pragma once

#include "BlockingQueue.h"
#include "NetworkScore.h"
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

    // Returns false when the packet could not be queued because the reorder
    // thread has not drained this connection's queue yet. The IO thread treats
    // that as backpressure and retries later; it must not discard the packet.
    bool processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data, int sourceConnIndex);

    // True when there is room in the per-connection receive queue. The IO
    // thread consults this before reading a socket so a full queue is expressed
    // as TCP backpressure instead of packet loss.
    bool canAcceptFromConnection(int connIndex) const;

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

    // Returns indices of connections that are no longer connected.
    // Used by the client watchdog to enumerate slots needing reconnection.
    std::vector<int> getDeadSlots() const;

    // Hot-swaps the connection at slotIndex with a new socket fd.
    // The IO and send threads pick up the new connection on their next iteration.
    // Returns false (and closes newFd) when the channel is already closing.
    bool replaceConnection(int slotIndex, SocketFd newFd);

    std::shared_ptr<MessageTracker> getMessageTracker() const { return messageTracker; }

    // Number of message ids assigned by send(), i.e. packets accepted into the
    // ordered stream. Exposed for diagnostics and tests.
    uint64_t getSentMessageCount() const { return lastSendMessageId.load(std::memory_order_relaxed); }

    std::vector<std::shared_ptr<SocketStatus>> getSocketStatuses() const { return socketStatuses; }

    /// Compute an aggregated network quality score from all per-connection TCP
    /// metrics, send statistics, degradation status, and queue depth.
    /// Safe to call from any thread.
    NetworkScore getNetworkScore() const;

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
    // Protects replacement of the worker pointers. close() resets them only after
    // joining both workers, breaking the callbacks' shared_ptr cycles; a concurrent
    // close() may still be checking isCurrentThread() while the first one resets.
    std::mutex workerThreadsMutex;
    // Serializes replaceConnection() against close() so a slot swap cannot
    // propagate into worker objects that close() is joining/destroying.
    std::mutex replaceMutex;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    BlockingQueueSp resendQueue; // dedicated queue for resend traffic (conns VC_FIRST_RESEND_CONN_INDEX..VC_TCP_CONNECTIONS-1)

    std::vector<std::shared_ptr<ConnSendStats>> connSendStats;

    // Jitter buffer between the IO thread and the reorder thread. Overflow does
    // not drop data: the IO thread stops reading that connection until the
    // reorder thread frees a slot, so the backlog is expressed as TCP
    // backpressure. 4096 absorbs a stalled receive callback (~46 ms at 1 Gbps)
    // without pausing the socket; 1024 overflowed on a busy link.
    static constexpr size_t CONN_RECEIVE_QUEUE_CAPACITY = 4096;
    std::vector<std::unique_ptr<SpscQueue<DeliveryItem, CONN_RECEIVE_QUEUE_CAPACITY>>> connReceiveQueues;
    std::atomic<uint64_t> reorderEnqueueSeq{0};
    std::mutex reorderMutex;
    std::condition_variable reorderCv;

    std::map<uint64_t, ReceivedItem> receivedDataMap;

    mutable std::mutex disconnectMutex;

    std::thread reorderThread;
    std::atomic<std::thread::id> reorderThreadId{}; // safe own-thread check for close()
    std::atomic<bool> reorderRunning{false};
    void reorderThreadFunc();

    std::atomic<bool> opened{false};
    // close() may be reached concurrently from the owner and the VC-wide
    // disconnect callback. Joining the same std::thread twice is undefined
    // behaviour (std::system_error/terminate), so the teardown runs exactly once.
    std::once_flag closeOnce;
    std::atomic<uint64_t> lastSendMessageId{0}; // uint64_t: long is 32-bit on Windows (MSVC)
    std::atomic<uint64_t> nextMessageId{0};
    std::chrono::steady_clock::time_point gapFirstSeen;
    bool gapTimerActive{false};
    int lastDeliveredConnIndex{-1};
    std::chrono::steady_clock::time_point lastHealthLogTime;
    std::chrono::milliseconds reorderTimeoutMs{4000};

    // Defensive fallback only: with backpressure a failed enqueue should not
    // happen. Aggregate instead of logging one flushed line per packet so a
    // storm cannot slow the producer thread further.
    uint64_t droppedRxPackets{0};
    std::chrono::steady_clock::time_point lastRxDropLogTime{};

    // Ingress drops are normal overload behaviour for a UDP tunnel, but logging
    // one flushed line per packet turns a drop burst into a log-I/O stall that
    // makes the overload worse. Report the aggregate once per interval.
    uint64_t droppedSendPackets{0};
    std::chrono::steady_clock::time_point lastSendDropLogTime{};

    std::vector<uint64_t> lastRxMessageId;
    std::vector<std::chrono::steady_clock::time_point> lastRxTime;
    std::vector<bool> lastRxValid;

    std::shared_ptr<MessageTracker> messageTracker;
    std::vector<std::shared_ptr<SocketStatus>> socketStatuses;

    std::unordered_set<uint64_t> notifiedMissingIds;
    std::chrono::steady_clock::time_point lastNotifyTime;
    std::chrono::milliseconds missingNotifyIntervalMs{200};

    // IDs enqueued for resend but not yet confirmed received by the peer.
    // Prevents re-enqueuing the same ID on every successive MISSING_NOTIFY.
    // Entries expire after pendingResendTtl so dropped resends can be retried.
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> pendingResendIds;
    static constexpr std::chrono::milliseconds pendingResendTtl{2000};

    SentDataCache sentDataCache;

    std::function<void(uint64_t messageId, const char *data, size_t size)> resendCallback;
    std::function<void(const std::vector<uint64_t> &missingIds)> missingNotifyCallback;
};
