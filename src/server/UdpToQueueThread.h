#ifndef UDP_TO_QUEUE_THREAD_H
#define UDP_TO_QUEUE_THREAD_H

#include <cstddef>
class UdpToQueueThread {
public:
    explicit UdpToQueueThread(int socket) : socket_(socket) {}
    void run();
private:
    size_t readFromUdpSocket(char* buffer, size_t bufferSize);
    void enqueueData(char* data, size_t length);

    int socket_;
};

#endif // UDP_TO_QUEUE_THREAD_H
