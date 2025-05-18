#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <atomic>
#include <cassert>
#include <functional>
#include <chrono>
#include <thread>

// A thread-safe memory pool for efficient buffer allocation and reuse
class MemoryPool {
public:
    static MemoryPool& getInstance();

    std::shared_ptr<std::vector<char>> getBuffer(size_t size);
    void recycleBuffer(std::shared_ptr<std::vector<char>> buffer);
    std::shared_ptr<std::vector<char>> getRecyclableBuffer(size_t size);

    struct MemoryStats {
        size_t totalAllocatedMemory;
        size_t totalBuffersCreated;
        size_t buffersRecycled;
        size_t smallBuffersAvailable;
        size_t mediumBuffersAvailable;
        size_t largeBuffersAvailable;
    };
    MemoryStats getStats();
    void trim(float percentToKeep = 0.5f);
    void setMaxPoolSize(size_t size);
    void setBufferSizes(size_t small, size_t medium, size_t large);

private:
    MemoryPool();
    ~MemoryPool();
    std::shared_ptr<std::vector<char>> getFromPool(size_t size);
    void trimQueue(std::mutex& mutex, std::queue<std::shared_ptr<std::vector<char>>>& queue, float percentToKeep);
    void startTrimThread();
    void stopTrimThread();

    // Buffer size categories
    size_t smallBufferSize_;
    size_t mediumBufferSize_;
    size_t largeBufferSize_;
    size_t maxPoolSize_;
    
    // Pools for different buffer sizes
    std::queue<std::shared_ptr<std::vector<char>>> smallBuffers_;
    std::mutex smallBuffersMutex_;
    
    std::queue<std::shared_ptr<std::vector<char>>> mediumBuffers_;
    std::mutex mediumBuffersMutex_;
    
    std::queue<std::shared_ptr<std::vector<char>>> largeBuffers_;
    std::mutex largeBuffersMutex_;
    
    // Statistics
    std::atomic<size_t> totalAllocatedMemory_;
    std::atomic<size_t> totalBuffersCreated_;
    std::atomic<size_t> buffersRecycled_;
    
    // Trim thread
    std::thread trimThread_;
    std::atomic<bool> trimThreadRunning_;
};

#endif // MEMORY_POOL_H