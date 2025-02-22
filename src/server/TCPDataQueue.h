#ifndef TCP_DATA_QUEUE_H
#define TCP_DATA_QUEUE_H

#include <queue>

class TCPDataQueue {
public:
    void enqueue(const std::vector<char>& data);
    std::vector<char> dequeue();
private:
    std::queue<std::vector<char>> queue;
};

#endif // TCP_DATA_QUEUE_H
