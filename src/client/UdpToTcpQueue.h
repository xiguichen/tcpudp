#ifndef UDPTOTCPQUEUE_H
#define UDPTOTCPQUEUE_H

#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

class UdpToTcpQueue {
public:
  void enqueue(const std::vector<char> &data);
  std::vector<char> dequeue();
  void cancel();

private:
  std::queue<std::vector<char>> queue;
  std::mutex mtx;
  std::condition_variable cv;
  bool shouldCancel = false;

  std::chrono::time_point<std::chrono::high_resolution_clock> lastEmitTime =
      std::chrono::high_resolution_clock::now();
  std::vector<char> bufferedNewData;
  void enqueueAndNotify(const std::vector<char> &data,
                        std::vector<char> &bufferedNewData);
  uint8_t sendId;
};

#endif // UDPTOTCPQUEUE_H
