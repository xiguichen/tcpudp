#ifndef UDP_TO_QUEUE_THREAD_H
#define UDP_TO_QUEUE_THREAD_H

#include "BlockingQueue.h"
#include <cstddef>
#include <memory>
#include <vector>
#include <MemoryPool.h>

class UdpToQueueThread {
public:
    UdpToQueueThread(int socket, BlockingQueue& queue)
        : socket_(socket), queue(queue) {}
    void run();
    
private:
    size_t readFromUdpSocket(char* buffer, size_t bufferSize);
    void enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer);

    int socket_;
    BlockingQueue& queue;
};

#endif // UDP_TO_QUEUE_THREAD_H
