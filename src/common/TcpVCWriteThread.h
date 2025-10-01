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

    void AckCallback(uint64_t messageId);

  protected:
    virtual void run();

  private:
    BlockingQueueSp writeQueue;
    TcpConnectionSp connection;
    BlockingQueueSp ackQueue;
    uint64_t lastMessageId = 0;
};

typedef std::shared_ptr<TcpVCWriteThread> TcpVCWriteThreadSp;
