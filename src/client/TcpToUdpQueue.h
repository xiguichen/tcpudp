#ifndef TCPTOUDPQUEUE_H
#define TCPTOUDPQUEUE_H

#include <vector>
#include <atomic>
#include <memory>
#include <chrono>
#include "../common/LockFreeQueue.h"
#include "../common/MemoryPool.h"
#include "../common/MemoryMonitor.h"

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
    // Lock-free queue for better performance
    LockFreeQueue<std::shared_ptr<std::vector<char>>> queue;
    
    // Atomic flag for signaling when data is available
    std::atomic<bool> dataAvailable{false};
    
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
