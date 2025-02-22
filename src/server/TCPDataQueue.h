#ifndef TCP_DATA_QUEUE_H
#define TCP_DATA_QUEUE_H

#include <queue>
#include <utility>
#include <vector>

class TcpDataQueue {
public:
    static TcpDataQueue& getInstance() {
        static TcpDataQueue instance;
        return instance;
    }

    void enqueue(int socket, const std::vector<char>& data);
    std::pair<int, std::vector<char>> dequeue();

private:
    std::queue<std::pair<int, std::vector<char>>> queue;

    TcpDataQueue() = default;
    ~TcpDataQueue() = default;
    TcpDataQueue(const TcpDataQueue&) = delete;
    TcpDataQueue& operator=(const TcpDataQueue&) = delete;
};

#endif // TCP_DATA_QUEUE_H
