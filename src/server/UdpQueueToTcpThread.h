#ifndef UDP_QUEUE_TO_TCP_THREAD_H
#define UDP_QUEUE_TO_TCP_THREAD_H

#include <vector>
#include <BlockingQueue.h>
#include <Socket.h>


class UdpQueueToTcpThread {
public:
    // Constructor that takes a tcpSocket as input
    explicit UdpQueueToTcpThread(SocketFd tcpSocket, BlockingQueue& udpDataQueue)
        : tcpSocket_(tcpSocket), udpDataQueue(udpDataQueue) {};
    void run();
private:
    void processDataConcurrently();
    void sendDataViaTcp(int tcpSocket, const std::vector<char>& data);
    SocketFd tcpSocket_; // Member variable to store the tcpSocket
    BlockingQueue& udpDataQueue;
};

#endif // UDP_QUEUE_TO_TCP_THREAD_H
