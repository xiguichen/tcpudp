#ifndef TCP_QUEUE_TO_UDP_THREAD_POOL_H
#define TCP_QUEUE_TO_UDP_THREAD_POOL_H

class TcpQueueToUdpThreadPool {
public:
    void run();
private:
    void processData();
    void sendDataViaUdp();
};

#endif // TCP_QUEUE_TO_UDP_THREAD_POOL_H
