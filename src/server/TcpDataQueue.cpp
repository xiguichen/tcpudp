#include "TcpDataQueue.h"
#include <thread>
#include "../common/PerformanceMonitor.h"

void TcpDataQueue::enqueue(int socket, const std::shared_ptr<std::vector<char>>& data) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    auto item = std::make_pair(socket, data);
    
    // Try to enqueue the item
    while (!queue.enqueue(item)) {
        // If queue is full, yield to allow consumers to process
        std::this_thread::yield();
    }
    
    // Signal that data is available
    dataAvailable.store(true, std::memory_order_release);
    
    // Record performance metrics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    PerformanceMonitor::getInstance().recordPacketProcessed(data->size(), duration);
}

std::pair<int, std::shared_ptr<std::vector<char>>> TcpDataQueue::dequeue() {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::pair<int, std::shared_ptr<std::vector<char>>> result;
    
    // Try to dequeue with a timeout
    while (!queue.dequeue_with_timeout(result, 100)) {
        // If timeout occurs, check if we should continue waiting
        if (!dataAvailable.load(std::memory_order_acquire)) {
            // No data is expected, yield and try again
            std::this_thread::yield();
        }
    }
    
    // If queue is now empty, update the dataAvailable flag
    if (queue.empty()) {
        dataAvailable.store(false, std::memory_order_release);
    }
    
    // Record performance metrics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    if (result.second) {
        PerformanceMonitor::getInstance().recordPacketProcessed(result.second->size(), duration);
    }
    
    return result;
}
