#pragma once

#include "TcpVCWriteThread.h"

class TcpVCWriteThreadFactory
{
  public:

    static TcpVCWriteThreadSp createThread(BlockingQueueSp writeQueue, TcpConnectionSp connection)
    {
        return std::make_shared<TcpVCWriteThread>(writeQueue, connection);
    }

};
