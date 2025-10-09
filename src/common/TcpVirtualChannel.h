#include "BlockingQueue.h"
#include "Socket.h"
#include "TcpVCReadThread.h"
#include "TcpVCWriteThread.h"
#include "VirtualChannel.h"
#include <map>
#include <memory>
#include <vector>

class TcpVirtualChannel : public VirtualChannel
{

  public:
    TcpVirtualChannel(std::vector<SocketFd> fds);

    // Method to open the channel
    virtual void open();

    // Method to send data through the channel
    virtual void send(const char *data, size_t size);

    // Method to check if the channel is open
    virtual bool isOpen() const;

    // Method to close the channel
    virtual void close();

    void processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data);

  private:
    std::vector<TcpVCReadThreadSp> readThreads;
    std::vector<TcpVCWriteThreadSp> writeThreads;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    std::map<uint64_t, std::shared_ptr<std::vector<char>>> receivedDataMap;
    std::mutex receivedDataMutex;

    bool opened = false;
    std::atomic<long> lastSendMessageId = 0;
    std::atomic<long> nextMessageId = 0;
};
