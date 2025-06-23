#pragma once
#include "StopableThread.h"

class SocketWriteThread: public StopableThread
{
  public:
    SocketWriteThread() = default;
    virtual ~SocketWriteThread();


  protected:
    // Write data to the socket
    virtual void write(const std::vector<char> &data) = 0;

    // Get the data for write
    virtual std::shared_ptr<std::vector<char>> getData() = 0;

    virtual void run();
};
