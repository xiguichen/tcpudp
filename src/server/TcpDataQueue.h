#ifndef TCP_DATA_QUEUE_H
#define TCP_DATA_QUEUE_H

#include <utility>
#include <vector>
#include <memory>
#include <atomic>
#include "../common/LockFreeQueue.h"

class TcpDataQueue {
public:
    static TcpDataQueue& getInstance() {
        static TcpDataQueue instance;
        return instance;
    }

    void enqueue(int socket, const std::shared_ptr<std::vector<char>>& data);
    std::pair<int, std::shared_ptr<std::vector<char>>> dequeue();

private:
    // Using our lock-free queue implementation
    LockFreeQueue<std::pair<int, std::shared_ptr<std::vector<char>>>> queue{4096};
    
    // Atomic flag for signaling when data is available
    std::atomic<bool> dataAvailable{false};

    TcpDataQueue() = default;
    ~TcpDataQueue() = default;
    TcpDataQueue(const TcpDataQueue&) = delete;
    TcpDataQueue& operator=(const TcpDataQueue&) = delete;
};

#endif // TCP_DATA_QUEUE_H
