#pragma once

#include "TcpVCWriteThread.h"

class TcpVCWriteThreadFactory
{
  public:

    static TcpVCWriteThreadSp createThread(BlockingQueueSp writeQueue, TcpConnectionSp connection, int connectionIndex,
                                            std::shared_ptr<ConnSendStats> sendStats = nullptr,
                                            std::shared_ptr<MessageTracker> messageTracker = nullptr,
                                            std::shared_ptr<SocketStatus> socketStatus = nullptr)
    {
        return std::make_shared<TcpVCWriteThread>(writeQueue, connection, connectionIndex, sendStats, messageTracker, socketStatus);
    }

};
