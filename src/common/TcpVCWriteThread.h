#pragma once

#include "BlockingQueue.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include <memory>

class TcpVCWriteThread : public StopableThread
{
  public:
    TcpVCWriteThread(BlockingQueueSp writeQueue, TcpConnectionSp connection)
        : writeQueue(writeQueue), connection(connection)
    {
    }
    virtual ~TcpVCWriteThread() = default;

  protected:
    virtual void run();

  private:
    BlockingQueueSp writeQueue;
    TcpConnectionSp connection;
    uint64_t lastMessageId = 0;
};

typedef std::shared_ptr<TcpVCWriteThread> TcpVCWriteThreadSp;
