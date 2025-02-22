#ifndef TCP_TO_QUEUE_THREAD_H
#define TCP_TO_QUEUE_THREAD_H

#include <cstddef>
class TcpToQueueThread {
public:
    explicit TcpToQueueThread(int socket) : socket_(socket) {}
    void run();
private:
    size_t readFromSocket(char* buffer, size_t bufferSize);
    void enqueueData(const char* data, size_t length);
    int socket_;
};

#endif // TCP_TO_QUEUE_THREAD_H
