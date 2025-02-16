#include "TcpToUdpQueue.h"

void TcpToUdpQueue::enqueue(const std::string& data) {
    std::lock_guard<std::mutex> lock(mtx);
    queue.push(data);
}

std::string TcpToUdpQueue::dequeue() {
    std::lock_guard<std::mutex> lock(mtx);
    if (queue.empty()) return "";
    std::string data = queue.front();
    queue.pop();
    return data;
}
