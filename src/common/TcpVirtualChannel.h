#include "BlockingQueue.h"
#include "Socket.h"
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

  private:
    struct ReceivedItem
    {
        std::shared_ptr<std::vector<char>> data;
        int sourceConnIndex{-1};
    };

    struct DeliveryItem
    {
        uint64_t messageId;
        std::shared_ptr<std::vector<char>> data;
        int sourceConnIndex;
    };

    std::vector<DeliveryItem> drainReceivedDataMap();

    std::vector<TcpVCReadThreadSp> readThreads;
    std::vector<TcpVCWriteThreadSp> writeThreads;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    std::map<uint64_t, ReceivedItem> receivedDataMap;
    std::timed_mutex receivedDataMutex;

    // Mutex to ensure thread safety for disconnection handling
    std::mutex disconnectMutex;

    // Dedicated delivery thread: read threads enqueue items (non-blocking),
    // this thread invokes receiveCallback to avoid blocking readers
    std::mutex deliveryMutex;
    std::condition_variable deliveryCv;
    std::vector<DeliveryItem> deliveryBuffer;
    std::thread deliveryThread;
    std::atomic<bool> deliveryRunning{false};
    void deliveryThreadFunc();

    // VC state
    std::atomic<bool> opened{false};
    std::atomic<long> lastSendMessageId{0};
    std::atomic<uint64_t> nextMessageId{0};
    std::chrono::steady_clock::time_point gapFirstSeen;
    bool gapTimerActive{false};
    int lastDeliveredConnIndex{-1};

    // Per-connection receive stats (updated under receivedDataMutex)
    std::vector<uint64_t> lastRxMessageId;
    std::vector<std::chrono::steady_clock::time_point> lastRxTime;
    std::vector<bool> lastRxValid;
};
