#include "TcpDataQueue.h"

#include <mutex>

void TcpDataQueue::enqueue(int socket, const std::shared_ptr<std::vector<char>>& data) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push(std::make_pair(socket, data));
    }
    cv.notify_one(); // Notify one waiting thread
}

std::pair<int, std::shared_ptr<std::vector<char>>> TcpDataQueue::dequeue() {
    std::unique_lock<std::mutex> lock(queueMutex);
    cv.wait(lock, [this] { return !queue.empty(); }); // Wait until the queue is not empty
    auto front = queue.front();
    queue.pop();
    return front;
}
