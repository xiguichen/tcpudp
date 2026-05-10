#pragma once

#include "BlockingQueue.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include "TcpVCWriteThread.h"
#include "VcProtocol.h"
#include <chrono>
#include <memory>
#include <vector>

class TcpVCSendThread : public StopableThread
{
  public:
    TcpVCSendThread(std::vector<TcpConnectionSp> connections,
                    BlockingQueueSp sendQueue,
                    std::vector<std::shared_ptr<ConnSendStats>> connSendStats,
                    std::shared_ptr<MessageTracker> messageTracker,
                    std::vector<std::shared_ptr<SocketStatus>> socketStatuses);

    virtual ~TcpVCSendThread();

  protected:
    virtual void run() override;

  private:
    void refreshConnRuntimeInfo(size_t connIndex);
    int rateConnection(size_t connIndex);

    static constexpr int TCP_RUNTIME_REFRESH_MS = 250;
    static constexpr int SEND_TIMEOUT_MS = 1800;
    static constexpr auto SLOW_SEND_WARN_MS = std::chrono::milliseconds(100);

    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    std::vector<std::shared_ptr<ConnSendStats>> connSendStats;
    std::shared_ptr<MessageTracker> messageTracker;
    std::vector<std::shared_ptr<SocketStatus>> socketStatuses;
    std::vector<std::chrono::steady_clock::time_point> lastRuntimeRefresh;
};

typedef std::shared_ptr<TcpVCSendThread> TcpVCSendThreadSp;
