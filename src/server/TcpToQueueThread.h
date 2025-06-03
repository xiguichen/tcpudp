#ifndef TCP_TO_QUEUE_THREAD_H
#define TCP_TO_QUEUE_THREAD_H

#include <cstddef>
#include <memory>
#include <vector>
#include <thread>
#include <MemoryPool.h>
#include <MemoryMonitor.h>

// Forward declaration
class UdpToQueueThread;

class TcpToQueueThread {
public:
    explicit TcpToQueueThread(int socket) : socket_(socket) {}
    void run();
private:
    size_t readFromSocket(char* buffer, size_t bufferSize);
    void enqueueData(const char* data, size_t length);
    void enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer);
    
    // Start a thread to handle UDP data
    void startUdpToQueueThread(int udpSocket, std::shared_ptr<BlockingQueue>& queue);
    
    int socket_;
};

#endif // TCP_TO_QUEUE_THREAD_H
