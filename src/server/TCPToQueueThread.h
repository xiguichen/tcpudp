#ifndef TCP_TO_QUEUE_THREAD_H
#define TCP_TO_QUEUE_THREAD_H

class TcpToQueueThread {
public:
    explicit TcpToQueueThread(int socket) : socket_(socket) {}
    void run();
private:
    void readFromSocket();
    void enqueueData();
    int socket_;
};

#endif // TCP_TO_QUEUE_THREAD_H
