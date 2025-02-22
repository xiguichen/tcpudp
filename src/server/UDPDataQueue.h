#ifndef UDP_DATA_QUEUE_H
#define UDP_DATA_QUEUE_H

#include <queue>
#include <utility>
#include <vector>

class UDPDataQueue {
public:
    static UDPDataQueue& getInstance() {
        static UDPDataQueue instance;
        return instance;
    }

    void enqueue(int socket, const std::vector<char>& data);
    std::pair<int, std::vector<char>> dequeue();

private:
    UDPDataQueue() = default;
    ~UDPDataQueue() = default;
    UDPDataQueue(const UDPDataQueue&) = delete;
    UDPDataQueue& operator=(const UDPDataQueue&) = delete;

    std::queue<std::pair<int, std::vector<char>>> queue;
};

#endif // UDP_DATA_QUEUE_H
