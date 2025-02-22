#include "UdpDataQueue.h"

void UdpDataQueue::enqueue(int socket, const std::vector<char>& data) {
    queue.push(std::make_pair(socket, data));
}

std::pair<int, std::vector<char>> UdpDataQueue::dequeue() {
    if (queue.empty()) {
        return std::make_pair(-1, std::vector<char>()); // Return an empty pair if the queue is empty
    }
    auto front = queue.front();
    queue.pop();
    return front;
}
