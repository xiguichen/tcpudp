#pragma once

#include "TcpVCWriteThread.h"

class TcpVCWriteThreadFactory
{
  public:

    static TcpVCWriteThreadSp createThread(BlockingQueueSp writeQueue, TcpConnectionSp connection, int connectionIndex)
    {
        return std::make_shared<TcpVCWriteThread>(writeQueue, connection, connectionIndex);
    }

};
