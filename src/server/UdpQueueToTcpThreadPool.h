#ifndef UDP_QUEUE_TO_TCP_THREAD_POOL_H
#define UDP_QUEUE_TO_TCP_THREAD_POOL_H

#include <memory>
#include <vector>

class UdpQueueToTcpThreadPool {
public:
    void run();
private:
    void processDataConcurrently();
    void sendDataViaTcp(int tcpSocket, const std::shared_ptr<std::vector<char>>& data);
    uint8_t sendId = 0;
};

#endif // UDP_QUEUE_TO_TCP_THREAD_POOL_H
