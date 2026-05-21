#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <memory>
#include <condition_variable>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

class BlockingQueue {
public:
  BlockingQueue() {};

  void enqueue(const std::shared_ptr<std::vector<char>> &data);
  std::shared_ptr<std::vector<char>> dequeue();
  std::shared_ptr<std::vector<char>> dequeueWithTimeout(int timeoutMs);
  std::shared_ptr<std::vector<char>> tryDequeue();
  void cancelWait();
  size_t size();
  /// Lock-free approximate size. Suitable for threshold checks where exact count is not required.
  size_t approxSize() const { return approxQueueSize.load(std::memory_order_relaxed); }

private:

  // Standard queue for UDP data
  std::queue<std::shared_ptr<std::vector<char>>> queue;
  std::mutex queueMutex; // Mutex for queue protection
  std::condition_variable queueCondVar; // Condition variable for producer-consumer
  bool cancelled = false;
  std::atomic<size_t> approxQueueSize{0}; // Lock-free size for threshold checks

};

typedef std::shared_ptr<BlockingQueue> BlockingQueueSp;
#endif // BLOCKING_QUEUE_H
