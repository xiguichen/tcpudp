#ifndef TCP_QUEUE_TO_UDP_THREAD_POOL_H
#define TCP_QUEUE_TO_UDP_THREAD_POOL_H

class TCPQueueToUDPThreadPool {
public:
    void run();
private:
    void processData();
    void sendDataViaUDP();
};

#endif // TCP_QUEUE_TO_UDP_THREAD_POOL_H
