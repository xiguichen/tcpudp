#include "TcpToUdpQueue.h"

void TcpToUdpQueue::enqueue(const std::vector<char> &data) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    queue.push(data);
  }
  cv.notify_one(); // Notify one waiting thread
}

std::vector<char> TcpToUdpQueue::dequeue() {
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [this] { return !queue.empty() || this->shouldCancel; }); // Wait until the queue is not empty
  if(this->shouldCancel) return {};
  std::vector<char> data = queue.front();
  queue.pop();
  return data;
}
void TcpToUdpQueue::cancel() {
  this->shouldCancel = true;
  this->cv.notify_all();
}

