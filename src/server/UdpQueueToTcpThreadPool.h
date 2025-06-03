#ifndef UDP_QUEUE_TO_TCP_THREAD_POOL_H
#define UDP_QUEUE_TO_TCP_THREAD_POOL_H

#include <vector>
#include <BlockingQueue.h>
#include <Socket.h>


class UdpQueueToTcpThreadPool {
public:
    // Constructor that takes a tcpSocket as input
    explicit UdpQueueToTcpThreadPool(SocketFd tcpSocket);
    void run();
private:
    void processDataConcurrently();
    void sendDataViaTcp(int tcpSocket, const std::vector<char>& data);
    SocketFd tcpSocket_; // Member variable to store the tcpSocket
    std::shared_ptr<BlockingQueue> udpDataQueue;
};

#endif // UDP_QUEUE_TO_TCP_THREAD_POOL_H
