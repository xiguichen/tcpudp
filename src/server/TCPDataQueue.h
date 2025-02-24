#ifndef TCP_DATA_QUEUE_H
#define TCP_DATA_QUEUE_H

#include <queue>
#include <utility>
#include <mutex>
#include <vector>
#include <condition_variable>

class TcpDataQueue {
public:
    static TcpDataQueue& getInstance() {
        static TcpDataQueue instance;
        return instance;
    }

    void enqueue(int socket, const std::shared_ptr<std::vector<char>>& data);
    std::pair<int, std::shared_ptr<std::vector<char>>> dequeue();

private:
    std::queue<std::pair<int, std::shared_ptr<std::vector<char>>>> queue;
    std::mutex queueMutex;

    std::condition_variable cv;

    TcpDataQueue() = default;
    ~TcpDataQueue() = default;
    TcpDataQueue(const TcpDataQueue&) = delete;
    TcpDataQueue& operator=(const TcpDataQueue&) = delete;
};

#endif // TCP_DATA_QUEUE_H
