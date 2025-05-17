#include "UdpToTcpQueue.h"
#include <Protocol.h>
#include <Log.h>
#include <format>
#include <thread>

using namespace Logger;

UdpToTcpQueue::UdpToTcpQueue() 
    : queue(8192),  // Use a larger capacity for better performance
      lastEmitTime(std::chrono::high_resolution_clock::now()) {
    
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
    
    #ifdef BUFFER_UDP_DATA
    // Get the current buffer size without locking
    size_t currentBufferSize = bufferedNewData->size();
    
    Log::getInstance().info(std::format("Previous buffer size: {}", currentBufferSize));
    
    // 1. If the buffered data size + data size greater than 1400, send it now
    if (currentBufferSize + data.size() > 1400) {
        Log::getInstance().info("Buffer full, sending data");
        this->enqueueAndNotify(data, bufferedNewData);
    }
    // 2. Check if enough time has passed to send the data
    else {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - lastEmitTime);
            
        if (duration.count() > 50) {
            Log::getInstance().info("Time threshold reached, sending data");
            this->enqueueAndNotify(data, bufferedNewData);
        }
        // Buffer the data for next time
        else {
            // Get a new buffer if needed
            if (bufferedNewData->capacity() < currentBufferSize + data.size() + 100) {
                // Get a larger buffer from the pool
                auto newBuffer = MemoryPool::getInstance().getBuffer(
                    (currentBufferSize + data.size()) * 2);
                
                // Copy existing data
                if (!bufferedNewData->empty()) {
                    newBuffer->insert(newBuffer->end(), 
                                     bufferedNewData->begin(), 
                                     bufferedNewData->end());
                }
                
                // Recycle the old buffer
                MemoryPool::getInstance().recycleBuffer(bufferedNewData);
                
                // Use the new buffer
                bufferedNewData = newBuffer;
            }
            
            // Append the data
            UvtUtils::AppendUdpData(data, sendId.fetch_add(1, std::memory_order_relaxed), *bufferedNewData);
            
            Log::getInstance().info(std::format("New buffer size: {}", bufferedNewData->size()));
        }
    }
    #else
        this->enqueueAndNotify(data, bufferedNewData);
    #endif
}

std::vector<char> UdpToTcpQueue::dequeue() {
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

void UdpToTcpQueue::cancel() {
    shouldCancel.store(true, std::memory_order_release);
    
    // Clear the queue and recycle all buffers
    std::shared_ptr<std::vector<char>> item;
    while (queue.dequeue(item)) {
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

void UdpToTcpQueue::enqueueAndNotify(const std::vector<char> &data,
                                    std::shared_ptr<std::vector<char>> &bufferedNewData) {
    // Get a new buffer from the memory pool
    auto newBuffer = MemoryPool::getInstance().getBuffer(
        (bufferedNewData->size() + data.size()) * 1.2);  // Add 20% extra space
    
    // Copy buffered data if any
    if (!bufferedNewData->empty()) {
        newBuffer->insert(newBuffer->end(), 
                         bufferedNewData->begin(), 
                         bufferedNewData->end());
        
        // Clear the buffer
        bufferedNewData->clear();
    }
    
    // Append new data
    UvtUtils::AppendUdpData(data, sendId.fetch_add(1, std::memory_order_relaxed), *newBuffer);
    
    // Try to enqueue the buffer
    while (!queue.enqueue(newBuffer)) {
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
    
    // Update last emit time
    lastEmitTime = std::chrono::high_resolution_clock::now();
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

