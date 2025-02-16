#ifndef UDPTOTCPQUEUE_H
#define UDPTOTCPQUEUE_H

#include <queue>
#include <string>
#include <mutex>

class UdpToTcpQueue {
public:
    void enqueue(const std::string& data);
    std::string dequeue();

private:
    std::queue<std::string> queue;
    std::mutex mtx;
};

#endif // UDPTOTCPQUEUE_H
