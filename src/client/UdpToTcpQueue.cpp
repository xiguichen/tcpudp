#include "UdpToTcpQueue.h"

void UdpToTcpQueue::enqueue(const std::vector<char>& data) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(data);
    }
    cv.notify_one(); // Notify one waiting thread
}

std::vector<char> UdpToTcpQueue::dequeue() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return !queue.empty(); }); // Wait until the queue is not empty
    std::vector<char> data = queue.front();
    queue.pop();
    return data;
}
