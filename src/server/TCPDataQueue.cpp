#include "TcpDataQueue.h"

void TcpDataQueue::enqueue(int socket, const std::shared_ptr<std::vector<char>>& data) {
    queue.push(std::make_pair(socket, data));
}

std::pair<int, std::shared_ptr<std::vector<char>>> TcpDataQueue::dequeue() {
    if (queue.empty()) {
        return std::make_pair(-1, std::make_shared<std::vector<char>>()); // Return an empty pair if the queue is empty
    }
    auto front = queue.front();
    queue.pop();
    return front;
}
