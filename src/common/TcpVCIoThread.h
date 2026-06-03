#pragma once

#include "Socket.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include "TcpVCWriteThread.h"
#include "VcProtocol.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
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

    // Hot-swap the connection at the given slot. The IO thread picks up the
    // new socket on its next poll iteration (within IO_POLL_TIMEOUT_MS).
    void replaceConnection(int slot, TcpConnectionSp conn);

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

    void readFromConnection(int connIndex, TcpConnectionSp conn);

    std::mutex connectionsMutex;
    std::vector<TcpConnectionSp> connections;
    // Bumped under connectionsMutex whenever a slot is replaced. The run() loop
    // re-copies its connection snapshot only when this changes, avoiding a 32-element
    // shared_ptr copy on every poll cycle under active traffic.
    std::atomic<uint64_t> connGeneration{0};
    std::vector<ReadBuffer> readBuffers;

    std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback;
    std::function<void(uint64_t)> resendRequestCallback;
    std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback;
    std::function<void(TcpConnectionSp)> disconnectCallback;
};
