#include "BlockingQueue.h"
#include <vector>
#include <memory>
#include "Log.h"
#include <format>

using namespace Logger;

void BlockingQueue::enqueue(const std::shared_ptr<std::vector<char>> &data) {
    // Enqueue to the standard queue with locking
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      queue.push(data);
      info(std::format("Queue size after enqueue: {}, Queue address: {:p}", queue.size(), static_cast<const void*>(this)));
    }

  info(std::format("Enqueued data, Queue address: {:p}", static_cast<const void*>(this)));
  queueCondVar.notify_one();
}

std::shared_ptr<std::vector<char>> BlockingQueue::dequeue() {

  info(std::format("Wait for queue data , Queue address: {:p}", static_cast<const void*>(this)));

  std::unique_lock<std::mutex> lock(queueMutex);
  // Block until queue is not empty
  queueCondVar.wait(lock, [this] { return !queue.empty(); });
  auto result = queue.front();
  queue.pop();

  lock.unlock();
  info(std::format("Dequeued data of size: {}, Queue address: {:p}", result->size(), static_cast<const void*>(this)));

  info(std::format("Queue size after dequeue: {}, Queue address: {:p}", queue.size(), static_cast<const void*>(this)));
  return result;
}
