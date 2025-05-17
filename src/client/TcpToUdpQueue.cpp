#include "TcpToUdpQueue.h"
#include <Log.h>
#include <format>
#include <thread>

using namespace Logger;

TcpToUdpQueue::TcpToUdpQueue() 
    : queue(8192) {  // Use a larger capacity for better performance
    
    // Start memory monitoring if not already started
    MemoryMonitor::getInstance().start();
}

TcpToUdpQueue::~TcpToUdpQueue() {
    // Ensure all buffers are properly recycled
    cancel();
    
    // Log memory stats
    auto stats = getStats();
    Log::getInstance().info(std::format(
        "TcpToUdpQueue stats - Enqueued: {}, Dequeued: {}, Peak size: {}, Avg packet: {} bytes",
        stats.totalEnqueued,
        stats.totalDequeued,
        stats.peakQueueSize,
        stats.averagePacketSize
    ));
}

void TcpToUdpQueue::enqueue(const std::vector<char> &data) {
    // Track this operation for statistics
    totalEnqueued.fetch_add(1, std::memory_order_relaxed);
    totalPacketSize.fetch_add(data.size(), std::memory_order_relaxed);
    
    // Get a buffer from the memory pool
    auto buffer = MemoryPool::getInstance().getBuffer(data.size());
    
    // Copy the data
    buffer->assign(data.begin(), data.end());
    
    // Try to enqueue the buffer
    while (!queue.enqueue(buffer)) {
        // If queue is full, yield to allow consumers to process
        std::this_thread::yield();
    }
    
    // Update statistics
    size_t currentSize = queue.size();
    size_t peak = peakQueueSize.load(std::memory_order_relaxed);
    if (currentSize > peak) {
        peakQueueSize.store(currentSize, std::memory_order_relaxed);
    }
    
    // Signal that data is available
    dataAvailable.store(true, std::memory_order_release);
}

std::vector<char> TcpToUdpQueue::dequeue() {
    auto startTime = std::chrono::steady_clock::now();
    
    std::shared_ptr<std::vector<char>> result;
    
    // Try to dequeue with a timeout
    while (!queue.dequeue_with_timeout(result, 100)) {
        // If timeout occurs, check if we should continue waiting
        if (shouldCancel.load(std::memory_order_acquire)) {
            return {};
        }
        
        if (!dataAvailable.load(std::memory_order_acquire)) {
            // No data is expected, yield and try again
            std::this_thread::yield();
        }
    }
    
    // If queue is now empty, update the dataAvailable flag
    if (queue.empty()) {
        dataAvailable.store(false, std::memory_order_release);
    }
    
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

void TcpToUdpQueue::cancel() {
    shouldCancel.store(true, std::memory_order_release);
    
    // Clear the queue and recycle all buffers
    std::shared_ptr<std::vector<char>> item;
    while (queue.dequeue(item)) {
        if (item) {
            MemoryPool::getInstance().recycleBuffer(item);
        }
    }
}

TcpToUdpQueue::QueueStats TcpToUdpQueue::getStats() const {
    QueueStats stats;
    stats.currentQueueSize = queue.size();
    stats.peakQueueSize = peakQueueSize.load(std::memory_order_relaxed);
    stats.totalEnqueued = totalEnqueued.load(std::memory_order_relaxed);
    stats.totalDequeued = totalDequeued.load(std::memory_order_relaxed);
    
    // Calculate average packet size
    size_t totalSize = totalPacketSize.load(std::memory_order_relaxed);
    size_t totalPackets = totalEnqueued.load(std::memory_order_relaxed);
    
    if (totalPackets > 0) {
        stats.averagePacketSize = totalSize / totalPackets;
    } else {
        stats.averagePacketSize = 0;
    }
    
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

