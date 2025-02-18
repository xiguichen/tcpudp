#ifndef UDPTOTCPQUEUE_H
#define UDPTOTCPQUEUE_H

#include <queue>
#include <vector>
#include <mutex>

class UdpToTcpQueue {
public:
    void enqueue(const std::vector<char>& data);
    std::vector<char> dequeue();

private:
    std::queue<std::vector<char>> queue;
    std::mutex mtx;
};

#endif // UDPTOTCPQUEUE_H
