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
        size_t readOffset = 0; // start of unprocessed data; avoids O(N) erase-from-front

        // Number of unprocessed bytes
        size_t available() const { return data.size() - readOffset; }
        // Pointer to unprocessed data
        char *begin() { return data.data() + readOffset; }
        // Advance past consumed bytes, compacting when offset is large
        void consume(size_t n)
        {
            readOffset += n;
            if (readOffset > 4096 && readOffset >= data.size() / 2)
            {
                data.erase(data.begin(), data.begin() + static_cast<ptrdiff_t>(readOffset));
                readOffset = 0;
            }
        }
    };

    void readFromConnection(int connIndex);

    std::vector<TcpConnectionSp> connections;
    std::vector<ReadBuffer> readBuffers;

    std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback;
    std::function<void(uint64_t)> resendRequestCallback;
    std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback;
    std::function<void(TcpConnectionSp)> disconnectCallback;
};
