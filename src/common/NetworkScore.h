#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/// Aggregated network quality score computed from per-connection TCP metrics,
/// send statistics, and queue depth. Designed to be comparable across runs so
/// code changes can be evaluated for their impact on network performance.
///
/// All scores are normalized to 0-100 (higher = better).
struct NetworkScore
{
    /// Composite score (weighted average of sub-scores).
    int overall = 0;

    /// Based on smoothed RTT across all live connections.
    int latency = 0;

    /// Based on congestion window and send throughput.
    int throughput = 0;

    /// Based on retransmissions, timeout episodes, and connection health.
    int stability = 0;

    /// Based on send-queue depth relative to the drop threshold.
    int queueHealth = 0;

    // ---- Raw metrics (for diagnostics / before-after comparison) ----

    double avgRttMs = 0.0;
    double avgCwndKB = 0.0;
    uint64_t totalTxCount = 0;
    uint64_t totalReenqueueCount = 0;
    int degradedCount = 0;
    int deadCount = 0;
    int aliveCount = 0;
    size_t sendQueueDepth = 0;

    /// One-line summary suitable for log output.
    /// Example:
    ///   "[SCORE] overall=85 latency=90 throughput=78 stability=88 queueHealth=95
    ///    | rtt=2.3ms cwnd=64KB tx=12345 reenq=3 degraded=0 dead=0 alive=32 qdepth=12"
    std::string format() const;
};
