#ifndef UDP_DATA_QUEUE_H
#define UDP_DATA_QUEUE_H

#include <chrono>
#include <memory>
#include <utility>
#include <vector>
#include <unordered_map>
#include <atomic>
#include "../common/LockFreeQueue.h"
#include <mutex> // Still needed for bufferedNewDataMap access

class UdpDataQueue {
public:
  static UdpDataQueue &getInstance() {
    static UdpDataQueue instance;
    return instance;
  }

  void enqueue(int socket, const std::shared_ptr<std::vector<char>> &data);
  std::pair<int, std::shared_ptr<std::vector<char>>> dequeue();

private:
  UdpDataQueue() {
    // Initialize with larger capacity for UDP traffic
    lastEmitTime = std::chrono::high_resolution_clock::now();
  }
  ~UdpDataQueue() = default;
  UdpDataQueue(const UdpDataQueue &) = delete;
  UdpDataQueue &operator=(const UdpDataQueue &) = delete;
  void enqueueAndNotify(int socket, const std::shared_ptr<std::vector<char>> &data, std::vector<char>& bufferedNewData);

  // Using our lock-free queue implementation with larger capacity for UDP
  LockFreeQueue<std::pair<int, std::shared_ptr<std::vector<char>>>> queue{8192};
  
  // Atomic flag for signaling when data is available
  std::atomic<bool> dataAvailable{false};
  
  // We still need a mutex for the buffered data map
  std::mutex bufferMutex;
  std::atomic<uint8_t> sendId{0};

  std::chrono::time_point<std::chrono::high_resolution_clock> lastEmitTime;
  std::unordered_map<int, std::vector<char>> bufferedNewDataMap;
};

#endif // UDP_DATA_QUEUE_H
