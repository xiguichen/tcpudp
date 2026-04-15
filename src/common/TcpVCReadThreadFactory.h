#pragma once

#include "TcpVCReadThread.h"
#include "TcpConnection.h"
class TcpVCReadThreadFactory
{
  public:

    static TcpVCReadThreadSp createThread(TcpConnectionSp connection, int connectionIndex)
    {
        return std::make_shared<TcpVCReadThread>(connection, connectionIndex);
    }

};
