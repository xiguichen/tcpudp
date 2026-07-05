#include "NetworkScoreCalculator.h"
#include "VcProtocol.h" // SEND_QUEUE_DROP_THRESHOLD
#include <algorithm>
#include <cmath>

NetworkScore NetworkScoreCalculator::compute(
    const std::vector<TcpConnection::TcpConnectionRuntimeInfo> &infos,
    const std::vector<std::shared_ptr<ConnSendStats>> &sendStats,
    const std::vector<std::shared_ptr<SocketStatus>> &statuses,
    size_t sendQueueDepth)
{
    NetworkScore result;

    // ---- Aggregate raw metrics across live connections ----
    int aliveCount = 0;
    int deadCount = 0;
    int degradedCount = 0;
    double totalRttUs = 0.0;
    double totalCwndBytes = 0.0;
    int rttSamples = 0;
    int cwndSamples = 0;
    uint64_t totalTxCount = 0;
    uint64_t totalReenqueueCount = 0;

    const size_t n = std::min({infos.size(), sendStats.size(), statuses.size()});

    for (size_t i = 0; i < n; ++i)
    {
        const auto &info = infos[i];

        bool alive = info.valid;

        // A connection without TCP runtime info may still be alive — check send stats.
        if (!info.supported)
        {
            if (i < sendStats.size() && sendStats[i] && sendStats[i]->valid.load(std::memory_order_acquire))
                alive = true;
        }

        if (alive)
        {
            aliveCount++;

            if (info.supported && info.smoothedRttUs > 0)
            {
                totalRttUs += static_cast<double>(info.smoothedRttUs);
                rttSamples++;
            }

            if (info.supported && info.congestionWindowBytes > 0)
            {
                totalCwndBytes += static_cast<double>(info.congestionWindowBytes);
                cwndSamples++;
            }

            // Check degradation status
            if (i < statuses.size() && statuses[i])
            {
                if (statuses[i]->isDegraded.load(std::memory_order_acquire))
                    degradedCount++;
            }
        }
        else
        {
            deadCount++;
        }

        // Aggregate send stats (available even for dead connections for diagnostics)
        if (i < sendStats.size() && sendStats[i])
        {
            totalTxCount += sendStats[i]->txCount.load(std::memory_order_relaxed);
            totalReenqueueCount += sendStats[i]->reenqueueCount.load(std::memory_order_relaxed);
        }
    }

    // ---- Compute averages ----
    double avgRttMs = rttSamples > 0 ? (totalRttUs / rttSamples) / 1000.0 : 0.0;
    double avgCwndKB = cwndSamples > 0 ? (totalCwndBytes / cwndSamples) / 1024.0 : 0.0;

    // ---- Score each dimension ----
    result.latency = scoreLatency(avgRttMs);
    result.throughput = scoreThroughput(avgCwndKB, totalTxCount, aliveCount, static_cast<int>(n));
    result.stability = scoreStability(totalReenqueueCount, totalTxCount, degradedCount, deadCount, aliveCount);
    result.queueHealth = scoreQueueHealth(sendQueueDepth);

    // ---- Composite score (weighted average) ----
    // Weights: latency 25%, throughput 25%, stability 35%, queueHealth 15%
    // Stability is weighted higher because the system's reliability depends on all 32
    // connections remaining healthy; a single degraded connection can trigger resend cascades.
    result.overall = static_cast<int>(
        result.latency * 0.25 +
        result.throughput * 0.25 +
        result.stability * 0.35 +
        result.queueHealth * 0.15);

    // ---- Populate raw metrics ----
    result.avgRttMs = avgRttMs;
    result.avgCwndKB = avgCwndKB;
    result.totalTxCount = totalTxCount;
    result.totalReenqueueCount = totalReenqueueCount;
    result.degradedCount = degradedCount;
    result.deadCount = deadCount;
    result.aliveCount = aliveCount;
    result.sendQueueDepth = sendQueueDepth;

    return result;
}

int NetworkScoreCalculator::scoreLatency(double avgRttMs)
{
    if (avgRttMs <= 0.0)
        return 100; // no data → assume best

    // Piecewise linear interpolation between thresholds.
    // Thresholds in ms: 0→100, 1→90, 5→75, 20→55, 50→35, 100→15, 200→0
    struct Threshold
    {
        double rttMs;
        int score;
    };
    static constexpr Threshold kThresholds[] = {
        {0.0, 100},
        {1.0, 90},
        {5.0, 75},
        {20.0, 55},
        {50.0, 35},
        {100.0, 15},
        {200.0, 0},
    };

    if (avgRttMs >= 200.0)
        return 0;

    // Find the bracket and interpolate.
    for (size_t i = 0; i < std::size(kThresholds) - 1; ++i)
    {
        if (avgRttMs >= kThresholds[i].rttMs && avgRttMs < kThresholds[i + 1].rttMs)
        {
            double range = kThresholds[i + 1].rttMs - kThresholds[i].rttMs;
            double ratio = (avgRttMs - kThresholds[i].rttMs) / range;
            double rawScore = kThresholds[i].score + ratio * (kThresholds[i + 1].score - kThresholds[i].score);
            return static_cast<int>(std::round(rawScore));
        }
    }

    return 0;
}

int NetworkScoreCalculator::scoreThroughput(double avgCwndKB, uint64_t totalTxCount,
                                            int aliveCount, int totalConnections)
{
    // If no data is flowing yet, score based on connection health alone.
    if (totalTxCount == 0)
        return aliveCount > 0 ? 70 : 0;

    // Connection availability: fraction of connections alive.
    double aliveRatio = totalConnections > 0
                            ? static_cast<double>(aliveCount) / static_cast<double>(totalConnections)
                            : 0.0;
    int connScore = static_cast<int>(aliveRatio * 100.0);

    // Congestion window score: larger window = higher potential throughput.
    // Typical LAN cwnd: 64KB-256KB, WAN: 16KB-128KB.
    int cwndScore = 0;
    if (avgCwndKB > 0.0)
    {
        if (avgCwndKB >= 128.0)
            cwndScore = 100;
        else if (avgCwndKB >= 64.0)
            cwndScore = 85;
        else if (avgCwndKB >= 32.0)
            cwndScore = 70;
        else if (avgCwndKB >= 16.0)
            cwndScore = 50;
        else if (avgCwndKB >= 8.0)
            cwndScore = 30;
        else
            cwndScore = 10;
    }
    else
    {
        // No cwnd data (e.g. Windows): score from connection health only.
        cwndScore = connScore;
    }

    // Blend: 60% cwnd, 40% connection availability.
    return static_cast<int>(cwndScore * 0.6 + connScore * 0.4);
}

int NetworkScoreCalculator::scoreStability(uint64_t totalReenqueueCount, uint64_t totalTxCount,
                                           int degradedCount, int deadCount, int aliveCount)
{
    // Start at 100, deduct for problems.
    int score = 100;

    // Deduct for dead connections (worst: can't send or receive).
    // Each dead connection costs up to 15 points.
    score -= deadCount * 15;

    // Deduct for degraded connections (can still send but unreliable).
    score -= degradedCount * 8;

    // Deduct for re-enqueue rate (send failures).
    if (totalTxCount > 0)
    {
        double reenqueueRate = static_cast<double>(totalReenqueueCount) / static_cast<double>(totalTxCount);
        // Re-enqueue rate penalties:
        //   <1%  → no penalty
        //   1-5% → slight penalty
        //   5-20%→ moderate penalty
        //   >20% → heavy penalty
        if (reenqueueRate > 0.20)
            score -= 40;
        else if (reenqueueRate > 0.10)
            score -= 30;
        else if (reenqueueRate > 0.05)
            score -= 20;
        else if (reenqueueRate > 0.01)
            score -= 10;
    }

    // If ALL connections are dead, score is 0 regardless.
    if (aliveCount == 0 && deadCount > 0)
        score = 0;

    return std::max(0, std::min(100, score));
}

int NetworkScoreCalculator::scoreQueueHealth(size_t sendQueueDepth)
{
    // 0 depth → 100, at SEND_QUEUE_DROP_THRESHOLD → 0, above → 0.
    if (sendQueueDepth == 0)
        return 100;
    if (sendQueueDepth >= SEND_QUEUE_DROP_THRESHOLD)
        return 0;

    // Linear interpolation: 100 at depth=0, 0 at depth=SEND_QUEUE_DROP_THRESHOLD
    double ratio = static_cast<double>(sendQueueDepth) / static_cast<double>(SEND_QUEUE_DROP_THRESHOLD);
    return static_cast<int>(std::round(100.0 * (1.0 - ratio)));
}
