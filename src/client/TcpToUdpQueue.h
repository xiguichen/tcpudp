#ifndef TCPTOTCPQUEUE_H
#define TCPTOTCPQUEUE_H
#include <queue>
#include <vector>
#include <mutex>
#include <queue>
class TcpToUdpQueue {
public:
    void enqueue(const std::vector<char>& data);
    std::vector<char> dequeue();
private:
        std::queue<std::vector<char>> queue;
        std::mutex mtx;
};
#endif // TCPTOTCPQUEUE_H
