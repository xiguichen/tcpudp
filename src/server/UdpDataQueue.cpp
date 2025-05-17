#include "UdpDataQueue.h"
#include <Log.h>
#include <Protocol.h>
#include <thread>
#include "../common/PerformanceMonitor.h"
using namespace Logger;

void UdpDataQueue::enqueue(int socket,
                           const std::shared_ptr<std::vector<char>> &data) {

#ifdef BUFFER_UDP_DATA
  // Lock only for accessing the buffer map
  std::vector<char>* bufferedNewData;
  {
    std::lock_guard<std::mutex> lock(bufferMutex);
    bufferedNewData = &bufferedNewDataMap[socket];
  }

  Log::getInstance().info(
      std::format("previous buffer size: {}", bufferedNewData->size()));

  // 1. If the buffered data size + data size great than 1400, let's send it now
  if (bufferedNewData->size() + data->size() > 1400) {
    Log::getInstance().info("Get enough data for send");
    this->enqueueAndNotify(socket, data, *bufferedNewData);
  }
  // 2. Check if we get enough time to send the data
  else {
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - lastEmitTime);
    if (duration.count() > 50) {
      Log::getInstance().info("Time reached for send");
      this->enqueueAndNotify(socket, data, *bufferedNewData);
    }
    // We should buffer the data for next time to send
    else {
      // Lock for modifying the buffer
      std::lock_guard<std::mutex> lock(bufferMutex);
      UvtUtils::AppendUdpData(*data, sendId.fetch_add(1, std::memory_order_relaxed), *bufferedNewData);
      Log::getInstance().info(
          std::format("new buffer size: {}", bufferedNewData->size()));
    }
  }

#else
    
    Log::getInstance().info("Buffer is not used for UDP data");
    std::vector<char>* bufferedNewData;
    {
      std::lock_guard<std::mutex> lock(bufferMutex);
      bufferedNewData = &bufferedNewDataMap[socket];
    }
    this->enqueueAndNotify(socket, data, *bufferedNewData);
#endif

}

std::pair<int, std::shared_ptr<std::vector<char>>> UdpDataQueue::dequeue() {
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

void UdpDataQueue::enqueueAndNotify(
    int socket, const std::shared_ptr<std::vector<char>> &data,
    std::vector<char> &bufferedNewData) {
  
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
  
  // Enqueue to the lock-free queue
  auto item = std::make_pair(socket, newData);
  while (!queue.enqueue(item)) {
    // If queue is full, yield to allow consumers to process
    std::this_thread::yield();
  }
  
  // Signal that data is available
  dataAvailable.store(true, std::memory_order_release);
  
  // Update last emit time
  lastEmitTime = std::chrono::high_resolution_clock::now();
  
  // Record performance metrics
  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
  PerformanceMonitor::getInstance().recordPacketProcessed(newData->size(), duration);
}
