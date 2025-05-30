#ifndef TCP_DATA_QUEUE_H
#define TCP_DATA_QUEUE_H

#include <utility>
#include <vector>
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
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
    // Standard queue for TCP data
    std::queue<std::pair<int, std::shared_ptr<std::vector<char>>>> queue;
    std::mutex queueMutex; // Mutex for queue protection
    std::condition_variable queueCondVar; // Condition variable for producer-consumer

    TcpDataQueue() = default;
    ~TcpDataQueue() = default;
    TcpDataQueue(const TcpDataQueue&) = delete;
    TcpDataQueue& operator=(const TcpDataQueue&) = delete;
};

#endif // TCP_DATA_QUEUE_H
