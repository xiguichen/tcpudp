#pragma once

#include <memory>
#include <vector>
#include "StopableThread.h"

class DataProcessThread : public StopableThread
{
  public:
    DataProcessThread() = default;

  protected:
    // Read data from the socket
    virtual std::shared_ptr<std::vector<char>> read() = 0;

    virtual void run();

    virtual void processData(const std::vector<char> &data) = 0;
};

