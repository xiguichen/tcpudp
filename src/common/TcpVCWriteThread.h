#pragma once

#include "BlockingQueue.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include <memory>

class TcpVCWriteThread : public StopableThread
{
  public:
    TcpVCWriteThread(BlockingQueueSp writeQueue, TcpConnectionSp connection, int connectionIndex)
        : writeQueue(writeQueue), connection(connection), connectionIndex(connectionIndex)
    {
    }
    virtual ~TcpVCWriteThread();

  protected:
    virtual void run();

  private:
    BlockingQueueSp writeQueue;
    TcpConnectionSp connection;
    int connectionIndex{-1};
};

typedef std::shared_ptr<TcpVCWriteThread> TcpVCWriteThreadSp;
