#pragma once
#include "StopableThread.h"
#include <functional>
#include <vector>
#include <memory>

class SocketWriteThread : public StopableThread
{
  public:
    SocketWriteThread() = default;
    virtual ~SocketWriteThread();

    // ack received function callback
    std::function<void(unsigned long)> ackReceivedCallback;

  protected:
    // Write data to the socket
    virtual void write(const std::vector<char> &data) = 0;

    // Get the data for write
    virtual std::shared_ptr<std::vector<char>> getData() = 0;

    virtual void run();
};
