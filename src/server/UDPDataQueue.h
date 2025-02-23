#ifndef UDP_DATA_QUEUE_H
#define UDP_DATA_QUEUE_H

#include <queue>
#include <memory>
#include <utility>
#include <vector>

class UdpDataQueue {
public:
    static UdpDataQueue& getInstance() {
        static UdpDataQueue instance;
        return instance;
    }

    void enqueue(int socket, const std::shared_ptr<std::vector<char>>& data);
    std::pair<int, std::shared_ptr<std::vector<char>>> dequeue();

private:
    UdpDataQueue() = default;
    ~UdpDataQueue() = default;
    UdpDataQueue(const UdpDataQueue&) = delete;
    UdpDataQueue& operator=(const UdpDataQueue&) = delete;

    std::queue<std::pair<int, std::shared_ptr<std::vector<char>>>> queue;
};

#endif // UDP_DATA_QUEUE_H
