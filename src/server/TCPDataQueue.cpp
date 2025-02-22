#include "TCPDataQueue.h"

void TCPDataQueue::enqueue(const std::vector<char>& data) {
    queue.push(data);
}

std::vector<char> TCPDataQueue::dequeue() {
    if (!queue.empty()) {
        auto data = queue.front();
        queue.pop();
        return data;
    }
    return {};
}
