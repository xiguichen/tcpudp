#ifndef TCP_DATA_QUEUE_H
#define TCP_DATA_QUEUE_H

#include <queue>
#include <utility>
#include <vector>

class TCPDataQueue {
public:
    static TCPDataQueue& getInstance() {
        static TCPDataQueue instance;
        return instance;
    }

    void enqueue(int socket, const std::vector<char>& data);
    std::pair<int, std::vector<char>> dequeue();

private:
    std::queue<std::pair<int, std::vector<char>>> queue;

    TCPDataQueue() = default;
    ~TCPDataQueue() = default;
    TCPDataQueue(const TCPDataQueue&) = delete;
    TCPDataQueue& operator=(const TCPDataQueue&) = delete;
};

#endif // TCP_DATA_QUEUE_H
