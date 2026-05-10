#pragma once

#include "Socket.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include "TcpVCWriteThread.h"
#include "VcProtocol.h"
#include <functional>
#include <memory>
#include <vector>

class TcpVCIoThread : public StopableThread
{
  public:
    TcpVCIoThread(std::vector<TcpConnectionSp> connections,
                  std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback,
                  std::function<void(uint64_t)> resendRequestCallback,
                  std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback,
                  std::function<void(TcpConnectionSp)> disconnectCallback);

    virtual ~TcpVCIoThread();

  protected:
    virtual void run() override;

  private:
    struct ReadBuffer
    {
        std::vector<char> data;
    };

    void readFromConnection(int connIndex);

    std::vector<TcpConnectionSp> connections;
    std::vector<ReadBuffer> readBuffers;

    std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback;
    std::function<void(uint64_t)> resendRequestCallback;
    std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback;
    std::function<void(TcpConnectionSp)> disconnectCallback;
};
