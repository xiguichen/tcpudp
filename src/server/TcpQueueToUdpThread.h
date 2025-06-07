#ifndef TCP_QUEUE_TO_UDP_THREAD_H
#define TCP_QUEUE_TO_UDP_THREAD_H

#include <vector>
#include <memory>
#include "BlockingQueue.h"
#include <Socket.h>

class TcpQueueToUdpThread {
public:
    TcpQueueToUdpThread(const SocketFd& udpSocket, BlockingQueue& queue) 
        : udpSocket_(udpSocket), queue_(queue) {}
    void run();
private:
    void processData();
    void sendDataViaUdp(std::shared_ptr<std::vector<char>> data);
    const SocketFd& udpSocket_;
    BlockingQueue& queue_;

};

#endif // TCP_QUEUE_TO_UDP_THREAD_H
