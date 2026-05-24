#pragma once

#include "BlockingQueue.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include "TcpVCWriteThread.h"
#include "VcProtocol.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

class TcpVCSendThread : public StopableThread
{
  public:
    TcpVCSendThread(std::vector<TcpConnectionSp> connections,
                    BlockingQueueSp sendQueue,
                    BlockingQueueSp resendQueue,
                    std::vector<std::shared_ptr<ConnSendStats>> connSendStats,
                    std::shared_ptr<MessageTracker> messageTracker,
                    std::vector<std::shared_ptr<SocketStatus>> socketStatuses);

    virtual ~TcpVCSendThread();

    // Hot-swap the connection at the given slot. The send thread picks it up
    // on the next packet (after the current dequeueWithTimeout returns).
    void replaceConnection(int slot, TcpConnectionSp conn,
                           std::shared_ptr<ConnSendStats> stats,
                           std::shared_ptr<SocketStatus> status);

  protected:
    virtual void run() override;

  private:
    void refreshConnRuntimeInfo(size_t connIndex, const std::vector<TcpConnectionSp>& conns);
    int rateConnection(size_t connIndex,
                       const std::vector<TcpConnectionSp>& conns,
                       const std::vector<std::shared_ptr<ConnSendStats>>& stats,
                       const std::vector<std::shared_ptr<SocketStatus>>& statuses);
    void sendOnResendConn(std::shared_ptr<std::vector<char>> data,
                          const std::vector<TcpConnectionSp>& conns);

    static constexpr int TCP_RUNTIME_REFRESH_MS = 250;

    std::mutex connectionsMutex;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    BlockingQueueSp resendQueue;
    std::vector<std::shared_ptr<ConnSendStats>> connSendStats;
    std::shared_ptr<MessageTracker> messageTracker;
    std::vector<std::shared_ptr<SocketStatus>> socketStatuses;
    std::vector<std::chrono::steady_clock::time_point> lastRuntimeRefresh;
    size_t roundRobinStart{0}; // rotates on each packet for tie-breaking among equal-scored connections
};

typedef std::shared_ptr<TcpVCSendThread> TcpVCSendThreadSp;
