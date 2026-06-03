#pragma once

#include "BlockingQueue.h"
#include "StopableThread.h"
#include "TcpConnection.h"
#include "TcpVCWriteThread.h"
#include "VcProtocol.h"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
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
    // on the next packet (it re-snapshots connections under the mutex per send).
    void replaceConnection(int slot, TcpConnectionSp conn,
                           std::shared_ptr<ConnSendStats> stats,
                           std::shared_ptr<SocketStatus> status);

    void setRunning(bool running) override;

  protected:
    virtual void run() override;

  private:
    void refreshConnRuntimeInfo(size_t connIndex, const std::vector<TcpConnectionSp>& conns);
    int rateConnection(size_t connIndex,
                       const std::vector<TcpConnectionSp>& conns,
                       const std::vector<std::shared_ptr<ConnSendStats>>& stats);
    void sendOnResendConn(std::shared_ptr<std::vector<char>> data,
                          const std::vector<TcpConnectionSp>& conns);

    static constexpr int TCP_RUNTIME_REFRESH_MS = 250;
    static constexpr int MAX_RESEND_RETRIES = 6;
    static constexpr size_t MAX_RESEND_BATCH = 8;
    static constexpr size_t MAX_RESEND_TRACKED = 2000;
    // Idle backstop: with the queue enqueue-notifier wiring, the thread is woken
    // immediately on new send/resend work, so this timeout is only a lost-wakeup
    // safety net (and lets the loop re-check isRunning() periodically).
    static constexpr int IDLE_WAIT_MS = 100;

    // Shared wake primitive for the idle wait. Held by both this thread and the
    // queues' enqueue notifiers via shared_ptr, so a queue can safely wake the
    // thread even if the thread object is being torn down (the Waker outlives it).
    struct Waker
    {
        std::mutex mtx;
        std::condition_variable cv;
    };
    std::shared_ptr<Waker> waker;

    std::mutex connectionsMutex;
    std::condition_variable connAvailableCv;
    std::vector<TcpConnectionSp> connections;
    BlockingQueueSp sendQueue;
    BlockingQueueSp resendQueue;
    std::vector<std::shared_ptr<ConnSendStats>> connSendStats;
    std::shared_ptr<MessageTracker> messageTracker;
    std::vector<std::shared_ptr<SocketStatus>> socketStatuses;
    std::vector<std::chrono::steady_clock::time_point> lastRuntimeRefresh;
    size_t roundRobinStart{0};
    size_t resendRoundRobin{0};
    std::unordered_map<uint64_t, uint8_t> resendRetryCount;
};

typedef std::shared_ptr<TcpVCSendThread> TcpVCSendThreadSp;
