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
        size_t idx = static_cast<size_t>(connectionIndex) % shards.size();
        std::lock_guard<std::mutex> lock(shards[idx].mutex);
        shards[idx].messageToConn[messageId] = connectionIndex;
    }

    int getConnectionIndex(uint64_t messageId)
    {
        for (auto &shard : shards)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto it = shard.messageToConn.find(messageId);
            if (it != shard.messageToConn.end())
                return it->second;
        }
        return -1;
    }

    void removeMessage(uint64_t messageId)
    {
        for (auto &shard : shards)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            shard.messageToConn.erase(messageId);
        }
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
    std::chrono::steady_clock::time_point degradedSince;
    std::atomic<int> degradationCount{0};

    void markDegraded()
    {
        isDegraded.store(true);
        degradedSince = std::chrono::steady_clock::now();
        degradationCount.fetch_add(1);
    }

    bool isCurrentlyDegraded(std::chrono::milliseconds maxDuration) const
    {
        if (!isDegraded.load())
        {
            return false;
        }
        auto elapsed = std::chrono::steady_clock::now() - degradedSince;
        int64_t baseMs = maxDuration.count();
        int count = degradationCount.load();
        int64_t effectiveMs = baseMs * (1LL << std::min(count - 1, 3));
        return elapsed < std::chrono::milliseconds(effectiveMs);
    }

    void recover()
    {
        isDegraded.store(false);
        degradationCount.store(0);
    }
};


