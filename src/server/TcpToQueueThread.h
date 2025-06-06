#ifndef TCP_TO_QUEUE_THREAD_H
#define TCP_TO_QUEUE_THREAD_H

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
    void enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer);
    
    SocketFd socket_;
    SocketFd udpSocket_;
    uint32_t clientId_;
    
};

#endif // TCP_TO_QUEUE_THREAD_H
