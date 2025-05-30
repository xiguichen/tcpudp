#ifndef TCPTOUDPQUEUE_H
#define TCPTOUDPQUEUE_H

#include <vector>
#include <atomic>
#include <memory>
#include <chrono>
#include <MemoryPool.h>
#include <MemoryMonitor.h>
#include <condition_variable>

class TcpToUdpQueue {
public:
    TcpToUdpQueue();
    ~TcpToUdpQueue();
    
    // Enqueue data for processing
    void enqueue(const std::vector<char>& data);
    
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
        size_t averagePacketSize;
        std::chrono::milliseconds avgWaitTime;
    };
    
    QueueStats getStats() const;

private:
    // Standard queue for TCP-to-UDP data
    std::queue<std::shared_ptr<std::vector<char>>> queue;
    std::mutex queueMutex; // Mutex for queue protection
    std::condition_variable queueCondVar; // Condition variable for producer-consumer
    
    // Cancellation flag
    std::atomic<bool> shouldCancel{false};
    
    // Statistics
    std::atomic<size_t> totalEnqueued{0};
    std::atomic<size_t> totalDequeued{0};
    std::atomic<size_t> peakQueueSize{0};
    std::atomic<size_t> totalPacketSize{0};
    std::atomic<uint64_t> totalWaitTimeMs{0};
    std::atomic<size_t> waitTimeCount{0};
};

#endif // TCPTOUDPQUEUE_H
