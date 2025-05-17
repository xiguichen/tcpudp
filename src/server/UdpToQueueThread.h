#ifndef UDP_TO_QUEUE_THREAD_H
#define UDP_TO_QUEUE_THREAD_H

#include <cstddef>
#include <memory>
#include <vector>
#include <MemoryPool.h>
#include <MemoryMonitor.h>

class UdpToQueueThread {
public:
    explicit UdpToQueueThread(int socket) : socket_(socket) {}
    void run();
    
private:
    size_t readFromUdpSocket(char* buffer, size_t bufferSize);
    void enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer);

    int socket_;
};

#endif // UDP_TO_QUEUE_THREAD_H
