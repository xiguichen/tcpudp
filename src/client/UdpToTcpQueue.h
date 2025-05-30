#ifndef UDPTOTCPQUEUE_H
#define UDPTOTCPQUEUE_H

#include <vector>
#include <chrono>
#include <atomic>
#include <memory>
#include <MemoryPool.h>
#include <MemoryMonitor.h>
#include <condition_variable>

class UdpToTcpQueue {
public:
    UdpToTcpQueue();
    ~UdpToTcpQueue();
    
    // Enqueue data for processing
    void enqueue(const std::vector<char> &data);
    
    // Dequeue data (returns empty vector if cancelled or timeout)
    std::vector<char> dequeue();
    
    // Cancel all pending operations
    void cancel();
    
    // Get queue statistics
    struct QueueStats {
        size_t currentQueueSize;
        size_t peakQueueSize;
        size_t totalEnqueued;
        size_t totalDequeued;
        size_t bufferSize;
        std::chrono::milliseconds avgWaitTime;
    };
    
    QueueStats getStats() const;

private:
    // Process and buffer data before enqueueing
    void enqueueAndNotify(const std::vector<char> &data,
                         std::shared_ptr<std::vector<char>> &bufferedNewData);
    
    // Standard queue for UDP-to-TCP data
    std::queue<std::shared_ptr<std::vector<char>>> queue;
    std::mutex queueMutex; // Mutex for queue protection
    std::condition_variable queueCondVar; // Condition variable for producer-consumer
    
    // Cancellation flag
    std::atomic<bool> shouldCancel{false};
    
    // Packet ID counter
    std::atomic<uint8_t> sendId{0};
    
    // Buffering state
    std::shared_ptr<std::vector<char>> bufferedNewData;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastEmitTime;
    
    // Statistics
    std::atomic<size_t> totalEnqueued{0};
    std::atomic<size_t> totalDequeued{0};
    std::atomic<size_t> peakQueueSize{0};
    std::atomic<uint64_t> totalWaitTimeMs{0};
    std::atomic<size_t> waitTimeCount{0};
};

#endif // UDPTOTCPQUEUE_H
