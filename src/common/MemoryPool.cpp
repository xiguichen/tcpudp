#include "MemoryPool.h"
#include "Log.h"
#include <format>

// Implementation of MemoryPool methods

MemoryPool& MemoryPool::getInstance() {
    static MemoryPool instance;
    return instance;
}

std::shared_ptr<std::vector<char>> MemoryPool::getBuffer(size_t size) {
    std::shared_ptr<std::vector<char>> buffer = getFromPool(size);
    if (buffer) {
        buffer->resize(size);
        return buffer;
    }
    buffer = std::make_shared<std::vector<char>>(size);
    totalAllocatedMemory_.fetch_add(size, std::memory_order_relaxed);
    totalBuffersCreated_.fetch_add(1, std::memory_order_relaxed);
    return buffer;
}

void MemoryPool::recycleBuffer(std::shared_ptr<std::vector<char>> buffer) {
    if (!buffer || buffer->empty()) {
        return;
    }
    size_t size = buffer->capacity();
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

std::shared_ptr<std::vector<char>> MemoryPool::getRecyclableBuffer(size_t size) {
    auto buffer = getBuffer(size);
    return std::shared_ptr<std::vector<char>>(
        buffer.get(),
        [this, original = buffer](std::vector<char>* ptr) {
            this->recycleBuffer(original);
        }
    );
}

MemoryPool::MemoryStats MemoryPool::getStats() {
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

void MemoryPool::trim(float percentToKeep) {
    trimQueue(smallBuffersMutex_, smallBuffers_, percentToKeep);
    trimQueue(mediumBuffersMutex_, mediumBuffers_, percentToKeep);
    trimQueue(largeBuffersMutex_, largeBuffers_, percentToKeep);
}

void MemoryPool::setMaxPoolSize(size_t size) {
    maxPoolSize_ = size;
}

void MemoryPool::setBufferSizes(size_t small, size_t medium, size_t large) {
    smallBufferSize_ = small;
    mediumBufferSize_ = medium;
    largeBufferSize_ = large;
}

MemoryPool::MemoryPool()
    : smallBufferSize_(1024),      // 1KB
      mediumBufferSize_(16384),    // 16KB
      largeBufferSize_(65536),     // 64KB
      maxPoolSize_(100),           // Maximum 100 buffers per category
      totalAllocatedMemory_(0),
      totalBuffersCreated_(0),
      buffersRecycled_(0) {
    startTrimThread();
}

MemoryPool::~MemoryPool() {
    stopTrimThread();
}

std::shared_ptr<std::vector<char>> MemoryPool::getFromPool(size_t size) {
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

void MemoryPool::trimQueue(std::mutex& mutex, std::queue<std::shared_ptr<std::vector<char>>>& queue, float percentToKeep) {
    std::lock_guard<std::mutex> lock(mutex);
    size_t currentSize = queue.size();
    size_t targetSize = static_cast<size_t>(currentSize * percentToKeep);
    while (queue.size() > targetSize) {
        queue.pop();
    }
}

void MemoryPool::startTrimThread() {
    trimThreadRunning_ = true;
    trimThread_ = std::thread([this]() {
        while (trimThreadRunning_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            trim(0.7f);  // Keep 70% of buffers
        }
    });
}

void MemoryPool::stopTrimThread() {
    trimThreadRunning_ = false;
    if (trimThread_.joinable()) {
        trimThread_.join();
    }
}

// Log memory pool statistics
void logMemoryPoolStats() {
    auto& pool = MemoryPool::getInstance();
    auto stats = pool.getStats();
    
    Logger::Log::getInstance().info(std::format(
        "Memory Pool Stats - Total Allocated: {}, Buffers Created: {}, Recycled: {}",
        stats.totalAllocatedMemory,
        stats.totalBuffersCreated,
        stats.buffersRecycled
    ));
    
    Logger::Log::getInstance().info(std::format(
        "Buffer Availability - Small: {}, Medium: {}, Large: {}",
        stats.smallBuffersAvailable,
        stats.mediumBuffersAvailable,
        stats.largeBuffersAvailable
    ));
}