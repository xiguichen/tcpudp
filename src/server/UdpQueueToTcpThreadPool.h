#ifndef UDP_QUEUE_TO_TCP_THREAD_POOL_H
#define UDP_QUEUE_TO_TCP_THREAD_POOL_H

class UdpQueueToTcpThreadPool {
public:
    void run();
private:
    void processDataConcurrently();
    void sendDataViaTCP();
};

#endif // UDP_QUEUE_TO_TCP_THREAD_POOL_H
