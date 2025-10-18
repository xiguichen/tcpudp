#pragma once

#include "TcpVCReadThread.h"
#include "TcpConnection.h"
class TcpVCReadThreadFactory
{
  public:

    static TcpVCReadThreadSp createThread(TcpConnectionSp connection)
    {
        return std::make_shared<TcpVCReadThread>(connection);
    }

};
