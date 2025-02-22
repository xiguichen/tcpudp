#ifndef UDP_TO_QUEUE_THREAD_H
#define UDP_TO_QUEUE_THREAD_H

class UdpToQueueThread {
public:
    explicit UdpToQueueThread(int socket) : socket_(socket) {}
    void run();
private:
    void readFromUDPSocket();
    void enqueueData();

    int socket_;
};

#endif // UDP_TO_QUEUE_THREAD_H
