#pragma once

#include "BlockingQueue.h"
#include "Socket.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include "TcpVCWriteThread.h"
#include "VcProtocol.h"
#include <functional>
#include <memory>
#include <vector>

class TcpVCIoThread : public StopableThread
{
  public:
    TcpVCIoThread(std::vector<TcpConnectionSp> connections,
                  BlockingQueueSp sendQueue,
                  std::vector<std::shared_ptr<ConnSendStats>> connSendStats,
                  std::shared_ptr<MessageTracker> messageTracker,
                  std::vector<std::shared_ptr<SocketStatus>> socketStatuses,
                  std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback,
                  std::function<void(uint64_t)> resendRequestCallback,
                  std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback,
                  std::function<void(TcpConnectionSp)> disconnectCallback);

    virtual ~TcpVCIoThread();

  protected:
    virtual void run() override;

  private:
    struct ReadBuffer
    {
        std::vector<char> data;
    };

    bool sendFromConnection(int connIndex, std::shared_ptr<std::vector<char>> dataVec);
    void readFromConnection(int connIndex);
    void setSocketNonBlocking(SocketFd fd);

    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    std::vector<std::shared_ptr<ConnSendStats>> connSendStats;
    std::shared_ptr<MessageTracker> messageTracker;
    std::vector<std::shared_ptr<SocketStatus>> socketStatuses;

    std::vector<ReadBuffer> readBuffers;

    std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback;
    std::function<void(uint64_t)> resendRequestCallback;
    std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback;
    std::function<void(TcpConnectionSp)> disconnectCallback;
};
