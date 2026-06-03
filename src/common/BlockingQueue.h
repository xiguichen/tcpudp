#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <memory>
#include <condition_variable>
#include <functional>
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

  /// Register an extra callback invoked (without holding the queue mutex) after each
  /// enqueue, in addition to the internal condition variable. Lets a consumer that
  /// waits on its own CV (e.g. one thread draining several queues) be woken on enqueue
  /// without polling. Pass nullptr to clear. Thread-safe.
  void setEnqueueNotifier(std::function<void()> notifier);

private:

  // Standard queue for UDP data
  std::queue<std::shared_ptr<std::vector<char>>> queue;
  std::mutex queueMutex; // Mutex for queue protection
  std::condition_variable queueCondVar; // Condition variable for producer-consumer
  bool cancelled = false;
  std::atomic<size_t> approxQueueSize{0}; // Lock-free size for threshold checks
  std::function<void()> enqueueNotifier;  // optional external wake, guarded by queueMutex

};

typedef std::shared_ptr<BlockingQueue> BlockingQueueSp;
#endif // BLOCKING_QUEUE_H
