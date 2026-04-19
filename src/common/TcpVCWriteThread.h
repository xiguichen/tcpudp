#pragma once

#include "BlockingQueue.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include <atomic>
#include <memory>

struct ConnSendStats
{
    std::atomic<uint64_t> lastTxMessageId{0};
    std::atomic<int64_t> lastTxTimeMs{0};
    std::atomic<uint64_t> txCount{0};
    std::atomic<uint64_t> reenqueueCount{0};
    std::atomic<uint64_t> slowSendCount{0};
    std::atomic<bool> valid{false};
};

class TcpVCWriteThread : public StopableThread
{
  public:
    TcpVCWriteThread(BlockingQueueSp writeQueue, TcpConnectionSp connection, int connectionIndex,
                     std::shared_ptr<ConnSendStats> sendStats)
        : writeQueue(writeQueue), connection(connection), connectionIndex(connectionIndex),
          sendStats(sendStats)
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
};

typedef std::shared_ptr<TcpVCWriteThread> TcpVCWriteThreadSp;
