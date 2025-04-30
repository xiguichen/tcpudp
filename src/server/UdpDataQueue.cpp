#include "UdpDataQueue.h"
#include <Protocol.h>

void UdpDataQueue::enqueue(int socket,
                           const std::shared_ptr<std::vector<char>> &data) {

  // Decide if we need to buffer the data for transfer or we should notify the
  // the consumer to consume the data
  std::vector<char> bufferedNewData = bufferedNewDataMap[socket];

  // 1. If the buffered data size + data size great than 1400, let's send it now
  if (bufferedNewData.size() + data->size() > 1400) {
      this->enqueueAndNotify(socket, data, bufferedNewData);
  }
  // 2. Check if we get enough time to send the data
  else {
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - lastEmitTime);
    if (duration.count() > 100) {
        this->enqueueAndNotify(socket, data, bufferedNewData);
    }
    // We should buffer the data for next time to send
    else {
      UvtUtils::AppendUdpData(*data, sendId++, bufferedNewData);
    }
  }
}

std::pair<int, std::shared_ptr<std::vector<char>>> UdpDataQueue::dequeue() {
  std::unique_lock<std::mutex> lock(queueMutex);
  cv.wait(lock, [this] {
    return !queue.empty();
  }); // Wait until the queue is not empty
  auto front = queue.front();
  queue.pop();
  return front;
}

void UdpDataQueue::enqueueAndNotify(
    int socket, const std::shared_ptr<std::vector<char>> &data, std::vector<char>& bufferedNewData) {
  std::lock_guard<std::mutex> lock(queueMutex);
  std::shared_ptr<std::vector<char>> newData =
      std::make_shared<std::vector<char>>(bufferedNewData.begin(),
                                          bufferedNewData.end());
  UvtUtils::AppendUdpData(*data, sendId++, *newData);
  queue.push(std::make_pair(socket, newData));
  cv.notify_one(); // Notify one waiting thread
  this->lastEmitTime = std::chrono::high_resolution_clock::now();
}
