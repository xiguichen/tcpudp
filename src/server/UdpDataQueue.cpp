#include "UdpDataQueue.h"
#include <Log.h>
#include <Protocol.h>
#include <thread>
#include <format>
#include "../common/PerformanceMonitor.h"
using namespace Logger;

void UdpDataQueue::enqueue(int socket,
                           const std::shared_ptr<std::vector<char>> &data) {   
    std::vector<char>* bufferedNewData;
    {
      std::lock_guard<std::mutex> lock(bufferMutex);
      bufferedNewData = &bufferedNewDataMap[socket];
    }
    this->enqueueAndNotify(socket, data, *bufferedNewData);
} // No change needed here, all logic is in enqueueAndNotify


std::pair<int, std::shared_ptr<std::vector<char>>> UdpDataQueue::dequeue() {
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

void UdpDataQueue::enqueueAndNotify(
    int socket, const std::shared_ptr<std::vector<char>> &data,
    std::vector<char> &bufferedNewData) {
  
  Log::getInstance().info("Enqueueing data to UDP queue...");
  auto startTime = std::chrono::high_resolution_clock::now();
  
  std::shared_ptr<std::vector<char>> newData =
      std::make_shared<std::vector<char>>();
  
  // Copy buffered data if any
  {
    std::lock_guard<std::mutex> lock(bufferMutex);
    if(!bufferedNewData.empty()) {
      newData->insert(newData->end(), bufferedNewData.begin(), bufferedNewData.end());
      // Clear the buffer now
      bufferedNewData.clear();
    }
  }
  
  // Append new data
  UvtUtils::AppendUdpData(*data, sendId.fetch_add(1, std::memory_order_relaxed), *newData);
  
  // Enqueue to the standard queue with locking
  auto item = std::make_pair(socket, newData);
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    queue.push(item);
  }
  queueCondVar.notify_one();

  // Update last emit time
  lastEmitTime = std::chrono::high_resolution_clock::now();

  // Record performance metrics
  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
  PerformanceMonitor::getInstance().recordPacketProcessed(newData->size(), duration);
}
