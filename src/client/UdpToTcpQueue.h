#ifndef UDPTOTCPQUEUE_H
#define UDPTOTCPQUEUE_H

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

class UdpToTcpQueue {
public:
    void enqueue(const std::vector<char>& data);
    std::vector<char> dequeue();
    void cancel();

  private:
    std::queue<std::vector<char>> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool shouldCancel = false;
};

#endif // UDPTOTCPQUEUE_H
