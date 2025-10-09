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
  void cancelWait();

private:

  // Standard queue for UDP data
  std::queue<std::shared_ptr<std::vector<char>>> queue;
  std::mutex queueMutex; // Mutex for queue protection
  std::condition_variable queueCondVar; // Condition variable for producer-consumer
  bool cancelled = false;

};

typedef std::shared_ptr<BlockingQueue> BlockingQueueSp;
#endif // BLOCKING_QUEUE_H
