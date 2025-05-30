#include "TcpDataQueue.h"
#include <thread>
#include "../common/PerformanceMonitor.h"

void TcpDataQueue::enqueue(int socket, const std::shared_ptr<std::vector<char>>& data) {
    auto startTime = std::chrono::high_resolution_clock::now();
    auto item = std::make_pair(socket, data);
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push(item);
    }
    queueCondVar.notify_one();
    // Record performance metrics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    PerformanceMonitor::getInstance().recordPacketProcessed(data->size(), duration);
}

std::pair<int, std::shared_ptr<std::vector<char>>> TcpDataQueue::dequeue() {
    auto startTime = std::chrono::high_resolution_clock::now();
    std::unique_lock<std::mutex> lock(queueMutex);
    // Block until queue is not empty
    queueCondVar.wait(lock, [this]{ return !queue.empty(); });
    std::pair<int, std::shared_ptr<std::vector<char>>> result = queue.front();
    queue.pop();
    lock.unlock();
    // Record performance metrics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    if (result.second) {
        PerformanceMonitor::getInstance().recordPacketProcessed(result.second->size(), duration);
    }
    return result;
}
