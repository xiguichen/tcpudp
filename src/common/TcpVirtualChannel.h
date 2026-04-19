#include "BlockingQueue.h"
#include "Socket.h"
#include "SpscQueue.h"
#include "TcpVCReadThread.h"
#include "TcpVCWriteThread.h"
#include "VirtualChannel.h"
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class TcpVirtualChannel : public VirtualChannel, public std::enable_shared_from_this<TcpVirtualChannel>
{

  public:
    TcpVirtualChannel(std::vector<SocketFd> fds);
    virtual ~TcpVirtualChannel()
    {
        this->close();
    };

    // Method to open the channel
    virtual void open();

    // Method to send data through the channel
    virtual void send(const char *data, size_t size);

    // Method to check if the channel is open
    virtual bool isOpen() const;

    // Method to close the channel
    virtual void close();

    void processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data, int sourceConnIndex);

    void setReorderTimeout(std::chrono::milliseconds timeout) { reorderTimeoutMs = timeout; }

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

    std::vector<TcpVCReadThreadSp> readThreads;
    std::vector<TcpVCWriteThreadSp> writeThreads;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;

    // Per-connection send stats (written by write threads, read by reorder thread)
    std::vector<std::shared_ptr<ConnSendStats>> connSendStats;

    // Lock-free per-connection receive queues (one SPSC queue per connection).
    // Each read thread is the sole producer for its queue; the reorder thread
    // is the sole consumer. No mutex needed on the data path.
    std::vector<std::unique_ptr<SpscQueue<DeliveryItem>>> connReceiveQueues;
    std::atomic<uint64_t> reorderEnqueueSeq{0};
    std::mutex reorderMutex;
    std::condition_variable reorderCv;

    // Reorder state — accessed only by the reorder thread (no lock needed)
    std::map<uint64_t, ReceivedItem> receivedDataMap;

    // Mutex to ensure thread safety for disconnection handling
    std::mutex disconnectMutex;

    // Reorder thread: drains per-connection SPSC queues, reorders messages,
    // checks gap timeouts, and invokes receiveCallback directly
    std::thread reorderThread;
    std::atomic<bool> reorderRunning{false};
    void reorderThreadFunc();

    // VC state
    std::atomic<bool> opened{false};
    std::atomic<long> lastSendMessageId{0};
    std::atomic<uint64_t> nextMessageId{0};
    std::chrono::steady_clock::time_point gapFirstSeen;
    bool gapTimerActive{false};
    int lastDeliveredConnIndex{-1};
    std::chrono::steady_clock::time_point lastHealthLogTime;
    std::chrono::milliseconds reorderTimeoutMs{4000};

    // Per-connection receive stats (accessed only by reorder thread)
    std::vector<uint64_t> lastRxMessageId;
    std::vector<std::chrono::steady_clock::time_point> lastRxTime;
    std::vector<bool> lastRxValid;
};
