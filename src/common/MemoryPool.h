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
    static MemoryPool& getInstance() {
        static MemoryPool instance;
        return instance;
    }

    // Get a buffer of the specified size
    std::shared_ptr<std::vector<char>> getBuffer(size_t size) {
        // Try to get a buffer from the pool first
        std::shared_ptr<std::vector<char>> buffer = getFromPool(size);
        
        if (buffer) {
            // Reset the buffer size to the requested size
            buffer->resize(size);
            return buffer;
        }
        
        // If no suitable buffer is available, create a new one
        buffer = std::make_shared<std::vector<char>>(size);
        
        // Track total allocated memory
        totalAllocatedMemory_.fetch_add(size, std::memory_order_relaxed);
        totalBuffersCreated_.fetch_add(1, std::memory_order_relaxed);
        
        return buffer;
    }
    
    // Return a buffer to the pool for reuse
    void recycleBuffer(std::shared_ptr<std::vector<char>> buffer) {
        if (!buffer || buffer->empty()) {
            return;
        }
        
        size_t size = buffer->capacity();
        
        // Only recycle buffers within our size categories
        if (size <= smallBufferSize_) {
            std::lock_guard<std::mutex> lock(smallBuffersMutex_);
            if (smallBuffers_.size() < maxPoolSize_) {
                buffer->clear();
                smallBuffers_.push(buffer);
                buffersRecycled_.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (size <= mediumBufferSize_) {
            std::lock_guard<std::mutex> lock(mediumBuffersMutex_);
            if (mediumBuffers_.size() < maxPoolSize_) {
                buffer->clear();
                mediumBuffers_.push(buffer);
                buffersRecycled_.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (size <= largeBufferSize_) {
            std::lock_guard<std::mutex> lock(largeBuffersMutex_);
            if (largeBuffers_.size() < maxPoolSize_) {
                buffer->clear();
                largeBuffers_.push(buffer);
                buffersRecycled_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Very large buffers are not recycled to avoid memory bloat
    }
    
    // Create a pre-sized buffer with a deleter that automatically recycles it
    std::shared_ptr<std::vector<char>> getRecyclableBuffer(size_t size) {
        auto buffer = getBuffer(size);
        
        // Create a new shared_ptr with a custom deleter that recycles the buffer
        return std::shared_ptr<std::vector<char>>(
            buffer.get(),
            [this, original = buffer](std::vector<char>* ptr) {
                // This keeps the original buffer alive until we're done with it
                this->recycleBuffer(original);
            }
        );
    }
    
    // Get memory usage statistics
    struct MemoryStats {
        size_t totalAllocatedMemory;
        size_t totalBuffersCreated;
        size_t buffersRecycled;
        size_t smallBuffersAvailable;
        size_t mediumBuffersAvailable;
        size_t largeBuffersAvailable;
    };
    
    MemoryStats getStats() {
        MemoryStats stats;
        stats.totalAllocatedMemory = totalAllocatedMemory_.load(std::memory_order_relaxed);
        stats.totalBuffersCreated = totalBuffersCreated_.load(std::memory_order_relaxed);
        stats.buffersRecycled = buffersRecycled_.load(std::memory_order_relaxed);
        
        {
            std::lock_guard<std::mutex> lock(smallBuffersMutex_);
            stats.smallBuffersAvailable = smallBuffers_.size();
        }
        {
            std::lock_guard<std::mutex> lock(mediumBuffersMutex_);
            stats.mediumBuffersAvailable = mediumBuffers_.size();
        }
        {
            std::lock_guard<std::mutex> lock(largeBuffersMutex_);
            stats.largeBuffersAvailable = largeBuffers_.size();
        }
        
        return stats;
    }
    
    // Trim the pool to reduce memory usage
    void trim(float percentToKeep = 0.5f) {
        trimQueue(smallBuffersMutex_, smallBuffers_, percentToKeep);
        trimQueue(mediumBuffersMutex_, mediumBuffers_, percentToKeep);
        trimQueue(largeBuffersMutex_, largeBuffers_, percentToKeep);
    }
    
    // Set the maximum pool size
    void setMaxPoolSize(size_t size) {
        maxPoolSize_ = size;
    }
    
    // Set buffer size categories
    void setBufferSizes(size_t small, size_t medium, size_t large) {
        smallBufferSize_ = small;
        mediumBufferSize_ = medium;
        largeBufferSize_ = large;
    }

private:
    MemoryPool() 
        : smallBufferSize_(1024),      // 1KB
          mediumBufferSize_(16384),    // 16KB
          largeBufferSize_(65536),     // 64KB
          maxPoolSize_(100),           // Maximum 100 buffers per category
          totalAllocatedMemory_(0),
          totalBuffersCreated_(0),
          buffersRecycled_(0) {
        // Start a background thread to periodically trim the pool
        startTrimThread();
    }
    
    ~MemoryPool() {
        stopTrimThread();
    }
    
    // Get a buffer from the pool based on size
    std::shared_ptr<std::vector<char>> getFromPool(size_t size) {
        if (size <= smallBufferSize_) {
            std::lock_guard<std::mutex> lock(smallBuffersMutex_);
            if (!smallBuffers_.empty()) {
                auto buffer = smallBuffers_.front();
                smallBuffers_.pop();
                return buffer;
            }
        } else if (size <= mediumBufferSize_) {
            std::lock_guard<std::mutex> lock(mediumBuffersMutex_);
            if (!mediumBuffers_.empty()) {
                auto buffer = mediumBuffers_.front();
                mediumBuffers_.pop();
                return buffer;
            }
        } else if (size <= largeBufferSize_) {
            std::lock_guard<std::mutex> lock(largeBuffersMutex_);
            if (!largeBuffers_.empty()) {
                auto buffer = largeBuffers_.front();
                largeBuffers_.pop();
                return buffer;
            }
        }
        
        return nullptr;
    }
    
    // Trim a queue to reduce memory usage
    void trimQueue(std::mutex& mutex, std::queue<std::shared_ptr<std::vector<char>>>& queue, float percentToKeep) {
        std::lock_guard<std::mutex> lock(mutex);
        
        size_t currentSize = queue.size();
        size_t targetSize = static_cast<size_t>(currentSize * percentToKeep);
        
        // Remove excess buffers
        while (queue.size() > targetSize) {
            queue.pop();
        }
    }
    
    // Start a background thread to periodically trim the pool
    void startTrimThread() {
        trimThreadRunning_ = true;
        trimThread_ = std::thread([this]() {
            while (trimThreadRunning_) {
                // Sleep for 30 seconds
                std::this_thread::sleep_for(std::chrono::seconds(30));
                
                // Trim the pool
                trim(0.7f);  // Keep 70% of buffers
            }
        });
    }
    
    // Stop the trim thread
    void stopTrimThread() {
        trimThreadRunning_ = false;
        if (trimThread_.joinable()) {
            trimThread_.join();
        }
    }

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