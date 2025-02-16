#ifndef TCPTOTCPQUEUE_H
#define TCPTOTCPQUEUE_H

#include <queue>
#include <string>
#include <mutex>

class TcpToUdpQueue {
public:
    void enqueue(const std::string& data);
    std::string dequeue();

private:
    std::queue<std::string> queue;
    std::mutex mtx;
};

#endif // TCPTOTCPQUEUE_H
