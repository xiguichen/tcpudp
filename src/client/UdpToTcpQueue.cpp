#include "UdpToTcpQueue.h"
#include <Protocol.h>
#include <Log.h>
#include <format>
#include <thread>
#include <PerformanceMonitor.h>

using namespace Logger;

UdpToTcpQueue::UdpToTcpQueue()
    : lastEmitTime(std::chrono::high_resolution_clock::now()) {
    
    // Initialize the buffer with memory from our pool
    bufferedNewData = MemoryPool::getInstance().getBuffer(0);
    
    // Start memory monitoring
    MemoryMonitor::getInstance().start();
    
    // Register the default alert handler
    MemoryMonitor::getInstance().registerDefaultAlertHandler();
}

UdpToTcpQueue::~UdpToTcpQueue() {
    // Ensure all buffers are properly recycled
    cancel();
    
    // Log final memory stats
    MemoryMonitor::getInstance().logMemoryUsage();
}

void UdpToTcpQueue::enqueue(const std::vector<char>& data) {
    // Track this operation for statistics
    totalEnqueued.fetch_add(1, std::memory_order_relaxed);
    this->enqueueAndNotify(data);
}

std::vector<char> UdpToTcpQueue::dequeue() {
    auto startTime = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(queueMutex);
    // Block until queue is not empty or shouldCancel is true
    queueCondVar.wait(lock, [this]{ return !queue.empty() || shouldCancel.load(std::memory_order_acquire); });
    if (shouldCancel.load(std::memory_order_acquire)) {
        return {};
    }
    std::shared_ptr<std::vector<char>> result = queue.front();
    queue.pop();
    lock.unlock();
    // Update statistics
    totalDequeued.fetch_add(1, std::memory_order_relaxed);
    // Calculate wait time
    auto endTime = std::chrono::steady_clock::now();
    auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    totalWaitTimeMs.fetch_add(waitTime.count(), std::memory_order_relaxed);
    waitTimeCount.fetch_add(1, std::memory_order_relaxed);
    // Return a copy of the data
    if (result) {
        std::vector<char> dataCopy(*result);
        // Recycle the buffer
        MemoryPool::getInstance().recycleBuffer(result);
        return dataCopy;
    }
    return {};
}

void UdpToTcpQueue::cancel() {
    shouldCancel.store(true, std::memory_order_release);
    
    // Clear the queue and recycle all buffers
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!queue.empty()) {
        auto item = queue.front();
        queue.pop();
        if (item) {
            MemoryPool::getInstance().recycleBuffer(item);
        }
    }
    // Recycle the buffered data
    if (bufferedNewData) {
        MemoryPool::getInstance().recycleBuffer(bufferedNewData);
        bufferedNewData = nullptr;
    }
}

void UdpToTcpQueue::enqueueAndNotify(const std::vector<char> &data) {

    auto startTime = std::chrono::high_resolution_clock::now();

    // Enqueue to the standard queue with locking
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push(std::make_shared<std::vector<char>>(data));
        size_t currentSize = queue.size();
        size_t peak = peakQueueSize.load(std::memory_order_relaxed);
        if (currentSize > peak) {
            peakQueueSize.store(currentSize, std::memory_order_relaxed);
        }
    }
    queueCondVar.notify_one();

    // Update last emit time
    lastEmitTime = std::chrono::high_resolution_clock::now();

    // Record performance metrics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    if (data.size()) {
        PerformanceMonitor::getInstance().recordPacketProcessed(data.size(), duration);
    }
}

UdpToTcpQueue::QueueStats UdpToTcpQueue::getStats() const {
    QueueStats stats;
    stats.currentQueueSize = queue.size();
    stats.peakQueueSize = peakQueueSize.load(std::memory_order_relaxed);
    stats.totalEnqueued = totalEnqueued.load(std::memory_order_relaxed);
    stats.totalDequeued = totalDequeued.load(std::memory_order_relaxed);
    stats.bufferSize = bufferedNewData ? bufferedNewData->size() : 0;
    
    // Calculate average wait time
    uint64_t totalMs = totalWaitTimeMs.load(std::memory_order_relaxed);
    size_t count = waitTimeCount.load(std::memory_order_relaxed);
    
    if (count > 0) {
        stats.avgWaitTime = std::chrono::milliseconds(totalMs / count);
    } else {
        stats.avgWaitTime = std::chrono::milliseconds(0);
    }
    
    return stats;
}

