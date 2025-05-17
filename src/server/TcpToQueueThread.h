#ifndef TCP_TO_QUEUE_THREAD_H
#define TCP_TO_QUEUE_THREAD_H

#include <cstddef>
#include <memory>
#include <vector>
#include <MemoryPool.h>
#include <MemoryMonitor.h>

class TcpToQueueThread {
public:
    explicit TcpToQueueThread(int socket) : socket_(socket) {}
    void run();
private:
    size_t readFromSocket(char* buffer, size_t bufferSize);
    void enqueueData(const char* data, size_t length);
    void enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer);
    int socket_;
};

#endif // TCP_TO_QUEUE_THREAD_H
