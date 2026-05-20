#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

struct ConnSendStats
{
    std::atomic<uint64_t> lastTxMessageId{0};
    std::atomic<int64_t> lastTxTimeMs{0};
    std::atomic<uint64_t> txCount{0};
    std::atomic<uint64_t> reenqueueCount{0};
    std::atomic<uint64_t> slowSendCount{0};
    std::atomic<bool> valid{false};
};

class MessageTracker
{
    struct Shard
    {
        std::mutex mutex;
        std::unordered_map<uint64_t, int> messageToConn;
    };

  public:
    MessageTracker() = default;
    explicit MessageTracker(size_t numShards) : shards(numShards) {}

    void recordMessage(uint64_t messageId, int connectionIndex)
    {
        size_t idx = messageId % shards.size();
        std::lock_guard<std::mutex> lock(shards[idx].mutex);
        shards[idx].messageToConn[messageId] = connectionIndex;
    }

    int getConnectionIndex(uint64_t messageId)
    {
        size_t idx = messageId % shards.size();
        std::lock_guard<std::mutex> lock(shards[idx].mutex);
        auto it = shards[idx].messageToConn.find(messageId);
        return it != shards[idx].messageToConn.end() ? it->second : -1;
    }

    void removeMessage(uint64_t messageId)
    {
        size_t idx = messageId % shards.size();
        std::lock_guard<std::mutex> lock(shards[idx].mutex);
        shards[idx].messageToConn.erase(messageId);
    }

    void clear()
    {
        for (auto &shard : shards)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            shard.messageToConn.clear();
        }
    }

  private:
    std::vector<Shard> shards;
};

struct SocketStatus
{
    int connectionIndex{-1};
    std::atomic<bool> isDegraded{false};
    std::atomic<int64_t> degradedSinceMs{0}; // ms since epoch; atomic to allow concurrent markDegraded/isCurrentlyDegraded
    std::atomic<int> degradationCount{0};

    void markDegraded()
    {
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
        degradedSinceMs.store(nowMs, std::memory_order_relaxed);
        isDegraded.store(true, std::memory_order_release);
        degradationCount.fetch_add(1);
    }

    bool isCurrentlyDegraded(std::chrono::milliseconds maxDuration) const
    {
        if (!isDegraded.load(std::memory_order_acquire))
            return false;
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
        int64_t sinceMs = degradedSinceMs.load(std::memory_order_relaxed);
        int64_t elapsedMs = nowMs - sinceMs;
        int64_t baseMs = maxDuration.count();
        int count = degradationCount.load(std::memory_order_relaxed);
        int64_t effectiveMs = baseMs * (1LL << std::min(count - 1, 3));
        return elapsedMs < effectiveMs;
    }

    void recover()
    {
        isDegraded.store(false, std::memory_order_release);
        degradationCount.store(0, std::memory_order_relaxed);
    }
};


