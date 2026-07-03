#pragma once

#include "NetworkScore.h"
#include "TcpConnection.h"
#include "TcpVCWriteThread.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

/// Computes a NetworkScore from raw per-connection TCP metrics, send statistics,
/// socket statuses, and queue depth. All methods are static and thread-safe
/// (they only read from the provided data, never mutate it).
class NetworkScoreCalculator
{
  public:
    /// Compute a full NetworkScore from all available metrics.
    /// @param infos       Per-connection TCP runtime info (one per connection).
    /// @param sendStats   Per-connection send statistics (one per connection).
    /// @param statuses    Per-connection socket degradation status (one per connection).
    /// @param sendQueueDepth  Current approximate depth of the send queue.
    static NetworkScore compute(const std::vector<TcpConnection::TcpConnectionRuntimeInfo> &infos,
                                const std::vector<std::shared_ptr<ConnSendStats>> &sendStats,
                                const std::vector<std::shared_ptr<SocketStatus>> &statuses,
                                size_t sendQueueDepth);

  private:
    /// Score latency from average smoothed RTT (0-100).
    /// <1ms→100, 1-5ms→90, 5-20ms→75, 20-50ms→55, 50-100ms→35, 100-200ms→15, >200ms→0
    static int scoreLatency(double avgRttMs);

    /// Score throughput from congestion window and send activity (0-100).
    static int scoreThroughput(double avgCwndKB, uint64_t totalTxCount,
                               int aliveCount, int totalConnections);

    /// Score stability from retransmissions, timeouts, degradations (0-100).
    static int scoreStability(uint64_t totalReenqueueCount, uint64_t totalTxCount,
                              int degradedCount, int deadCount, int aliveCount);

    /// Score queue health from send-queue depth (0-100).
    static int scoreQueueHealth(size_t sendQueueDepth);
};
