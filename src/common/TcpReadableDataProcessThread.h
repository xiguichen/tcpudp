#pragma once
#include "Socket.h"
#include "ReadableDataProcessThread.h"

class TcpReadableDataProcessThread : public ReadableDataProcessThread
{
  public:
    TcpReadableDataProcessThread() = default;
    ~TcpReadableDataProcessThread() override = default;

  protected:
    // Read data from the TCP socket
    std::shared_ptr<std::vector<char>> read() override;

  private:
    SocketFd _socketFd; // Socket file descriptor
};
