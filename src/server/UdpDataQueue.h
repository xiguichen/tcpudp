#ifndef UDP_DATA_QUEUE_H
#define UDP_DATA_QUEUE_H

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>
#include <map>

class UdpDataQueue {
public:
  static UdpDataQueue &getInstance() {
    static UdpDataQueue instance;
    return instance;
  }

  void enqueue(int socket, const std::shared_ptr<std::vector<char>> &data);
  std::pair<int, std::shared_ptr<std::vector<char>>> dequeue();

private:
  UdpDataQueue() = default;
  ~UdpDataQueue() = default;
  UdpDataQueue(const UdpDataQueue &) = delete;
  UdpDataQueue &operator=(const UdpDataQueue &) = delete;
  void enqueueAndNotify(int socket, const std::shared_ptr<std::vector<char>> &data, std::vector<char>& bufferedNewData);

  std::queue<std::pair<int, std::shared_ptr<std::vector<char>>>> queue;
  std::mutex queueMutex;
  std::condition_variable cv;
  uint8_t sendId = 0;

  std::chrono::time_point<std::chrono::high_resolution_clock> lastEmitTime =
      std::chrono::high_resolution_clock::now();
  std::map<int, std::vector<char>> bufferedNewDataMap;
};

#endif // UDP_DATA_QUEUE_H
