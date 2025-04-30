#include "UdpToTcpQueue.h"
#include <Protocol.h>

void UdpToTcpQueue::enqueue(const std::vector<char>& data) {

  // Decide if we need to buffer the data for transfer or we should notify the
  // the consumer to consume the data

  Log::getInstance().info(
      std::format("previous buffer size: {}", bufferedNewData.size()));

  // 1. If the buffered data size + data size great than 1400, let's send it now
  if (bufferedNewData.size() + data.size() > 1400) {
    Log::getInstance().info("Get enough data for send");
    this->enqueueAndNotify(data, bufferedNewData);
  }
  // 2. Check if we get enough time to send the data
  else {
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - lastEmitTime);
    if (duration.count() > 50) {
      Log::getInstance().info("Time reached for send");
      this->enqueueAndNotify(data, bufferedNewData);
    }
    // We should buffer the data for next time to send
    else {
      UvtUtils::AppendUdpData(data, sendId++, bufferedNewData);
      Log::getInstance().info(
          std::format("new buffer size: {}", bufferedNewData.size()));
    }
  }
}

std::vector<char> UdpToTcpQueue::dequeue() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return !queue.empty() || this->shouldCancel; }); // Wait until the queue is not empty
    if(this->shouldCancel) return {};
    std::vector<char> data = queue.front();
    queue.pop();
    return data;
}
void UdpToTcpQueue::cancel() {
  this->shouldCancel = true;
  this->cv.notify_all();
}

void UdpToTcpQueue::enqueueAndNotify(const std::vector<char> &data,
                                     std::vector<char> &bufferedNewData)
{
  std::lock_guard<std::mutex> lock(mtx);
  std::vector<char> newData;
  if(!bufferedNewData.empty())
  {
    newData.insert(newData.end(), bufferedNewData.begin(), bufferedNewData.end());
    // We should clear the buffer now
    bufferedNewData.clear();
  }
  UvtUtils::AppendUdpData(data, sendId++, newData);
  queue.push(newData);
  // Notify one waiting thread
  cv.notify_one();
  this->lastEmitTime = std::chrono::high_resolution_clock::now();
}

