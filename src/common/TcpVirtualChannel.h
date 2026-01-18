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

    void processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data);

    // Set the disconnect callback
    void setDisconnectCallback(std::function<void(TcpConnectionSp connection)> callback);

  private:
    std::vector<TcpVCReadThreadSp> readThreads;
    std::vector<TcpVCWriteThreadSp> writeThreads;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    std::map<uint64_t, std::shared_ptr<std::vector<char>>> receivedDataMap;
    std::mutex receivedDataMutex;
    // Disconnect callback to manage disconnections across the virtual channel
    std::function<void(TcpConnectionSp connection)> disconnectCallback;

    // Mutex to ensure thread safety for disconnection handling
    std::mutex disconnectMutex;

    bool opened = false;
    std::atomic<long> lastSendMessageId = 0;
    std::atomic<long> nextMessageId = 0;
};
