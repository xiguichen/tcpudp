#include "TcpDataQueue.h"

#include <mutex>

void TcpDataQueue::enqueue(int socket, const std::shared_ptr<std::vector<char>>& data) {
    std::lock_guard<std::mutex> lock(queueMutex);
    queue.push(std::make_pair(socket, data));
}

std::pair<int, std::shared_ptr<std::vector<char>>> TcpDataQueue::dequeue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (queue.empty()) {
        return std::make_pair(-1, std::make_shared<std::vector<char>>()); // Return an empty pair if the queue is empty
    }
    auto front = queue.front();
    queue.pop();
    return front;
}
