#ifndef UDP_DATA_QUEUE_H
#define UDP_DATA_QUEUE_H

#include <chrono>
#include <memory>
#include <condition_variable>
#include <utility>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <queue>
#include <mutex> // For producer-consumer synchronization
#include <condition_variable> // For producer-consumer synchronization

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

  // Standard queue for UDP data
  std::queue<std::pair<int, std::shared_ptr<std::vector<char>>>> queue;
  std::mutex queueMutex; // Mutex for queue protection
  std::condition_variable queueCondVar; // Condition variable for producer-consumer

  // We still need a mutex for the buffered data map
  std::mutex bufferMutex;
  std::atomic<uint8_t> sendId{0};

  std::chrono::time_point<std::chrono::high_resolution_clock> lastEmitTime;
  std::unordered_map<int, std::vector<char>> bufferedNewDataMap;
};

#endif // UDP_DATA_QUEUE_H
