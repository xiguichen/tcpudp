#pragma once
#include "Socket.h"
#include "DataProcessThread.h"

class TcpDataProcessThread : public DataProcessThread
{
  public:
    TcpDataProcessThread() = default;
    ~TcpDataProcessThread() override = default;

  protected:
    // Read data from the TCP socket
    std::shared_ptr<std::vector<char>> read() override;

  private:
    SocketFd _socketFd; // Socket file descriptor
};
