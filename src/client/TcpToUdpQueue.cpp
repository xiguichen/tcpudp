#include "TcpToUdpQueue.h"

void TcpToUdpQueue::enqueue(const std::vector<char> &data) {
  std::lock_guard<std::mutex> lock(mtx);
  queue.push(data);
}

std::vector<char> TcpToUdpQueue::dequeue() {
  std::lock_guard<std::mutex> lock(mtx);
  if (queue.empty())
    return {};
  std::vector<char> data = queue.front();
  queue.pop();
  return data;
}
