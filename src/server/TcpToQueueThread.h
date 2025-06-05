#ifndef TCP_TO_QUEUE_THREAD_H
#define TCP_TO_QUEUE_THREAD_H

#include <cstddef>
#include <memory>
#include <vector>
#include <MemoryPool.h>
#include <MemoryMonitor.h>
#include <BlockingQueue.h>
#include <Socket.h>

class TcpToQueueThread {
public:
    explicit TcpToQueueThread(int socket, int udpSocket, uint32_t clientId) : socket_(socket), udpSocket_(udpSocket), clientId_(clientId) {}
    void run();
private:
    size_t readFromSocket(char* buffer, size_t bufferSize);
    void enqueueData(const char* data, size_t length);
    void enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer);
    
    // Start a thread to handle UDP data
    void startUdpToQueueThread(int udpSocket, std::shared_ptr<BlockingQueue>& queue);
    
    SocketFd socket_;
    SocketFd udpSocket_;
    uint32_t clientId_;
    
};

#endif // TCP_TO_QUEUE_THREAD_H
