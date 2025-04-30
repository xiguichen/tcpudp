#include "UdpDataQueue.h"
#include <Log.h>
#include <Protocol.h>
using namespace Logger;

void UdpDataQueue::enqueue(int socket,
                           const std::shared_ptr<std::vector<char>> &data) {

  // Decide if we need to buffer the data for transfer or we should notify the
  // the consumer to consume the data
  std::vector<char> bufferedNewData = bufferedNewDataMap[socket];

  Log::getInstance().info(
      std::format("previous buffer size: {}", bufferedNewData.size()));

  // 1. If the buffered data size + data size great than 1400, let's send it now
  if (bufferedNewData.size() + data->size() > 1400) {
    Log::getInstance().info("Get enough data for send");
    this->enqueueAndNotify(socket, data, bufferedNewData);
  }
  // 2. Check if we get enough time to send the data
  else {
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - lastEmitTime);
    if (duration.count() > 100) {
      Log::getInstance().info("Time reached for send");
      this->enqueueAndNotify(socket, data, bufferedNewData);
    }
    // We should buffer the data for next time to send
    else {
      UvtUtils::AppendUdpData(*data, sendId++, bufferedNewData);
      Log::getInstance().info(
          std::format("new buffer size: {}", bufferedNewData.size()));
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
    int socket, const std::shared_ptr<std::vector<char>> &data,
    std::vector<char> &bufferedNewData) {
  std::lock_guard<std::mutex> lock(queueMutex);
  std::shared_ptr<std::vector<char>> newData =
      std::make_shared<std::vector<char>>();
  if(!bufferedNewData.empty())
  {
    UvtUtils::AppendUdpData(bufferedNewData, sendId++, *newData);
    // We should clear the buffer now
    bufferedNewData.clear();
  }
  UvtUtils::AppendUdpData(*data, sendId++, *newData);
  queue.push(std::make_pair(socket, newData));
  // Notify one waiting thread
  cv.notify_one();
  this->lastEmitTime = std::chrono::high_resolution_clock::now();
}
