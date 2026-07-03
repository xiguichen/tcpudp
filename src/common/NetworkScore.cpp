#include "NetworkScore.h"
#include <format>
#include <string>

std::string NetworkScore::format() const
{
    return std::format(
        "[SCORE] overall={} latency={} throughput={} stability={} queueHealth={} "
        "| rtt={:.1f}ms cwnd={:.0f}KB tx={} reenq={} degraded={} dead={} alive={} qdepth={}",
        overall, latency, throughput, stability, queueHealth,
        avgRttMs, avgCwndKB, totalTxCount, totalReenqueueCount,
        degradedCount, deadCount, aliveCount, sendQueueDepth);
}
