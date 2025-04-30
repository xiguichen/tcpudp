#ifndef UDP_QUEUE_TO_TCP_THREAD_POOL_H
#define UDP_QUEUE_TO_TCP_THREAD_POOL_H

#include <memory>
#include <vector>

class UdpQueueToTcpThreadPool {
public:
    void run();
private:
    void processDataConcurrently();
    void sendDataViaTcp(int tcpSocket, const std::vector<char>& data);
};

#endif // UDP_QUEUE_TO_TCP_THREAD_POOL_H
