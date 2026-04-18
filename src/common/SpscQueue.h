#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

template <typename T, size_t Capacity = 1024>
class SpscQueue
{
  public:
    SpscQueue() : buffer(std::make_unique<T[]>(Capacity)) {}

    SpscQueue(const SpscQueue &) = delete;
    SpscQueue &operator=(const SpscQueue &) = delete;

    bool try_enqueue(T item)
    {
        auto w = writePos.load(std::memory_order_relaxed);
        auto nextW = (w + 1) % Capacity;
        if (nextW == readPos.load(std::memory_order_acquire))
            return false;
        buffer[w] = std::move(item);
        writePos.store(nextW, std::memory_order_release);
        return true;
    }

    bool try_dequeue(T &item)
    {
        auto r = readPos.load(std::memory_order_relaxed);
        if (r == writePos.load(std::memory_order_acquire))
            return false;
        item = std::move(buffer[r]);
        readPos.store((r + 1) % Capacity, std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return readPos.load(std::memory_order_acquire) == writePos.load(std::memory_order_acquire);
    }

  private:
    alignas(64) std::atomic<size_t> writePos{0};
    alignas(64) std::atomic<size_t> readPos{0};
    std::unique_ptr<T[]> buffer;
};
