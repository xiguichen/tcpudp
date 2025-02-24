#ifndef TCP_QUEUE_TO_UDP_THREAD_POOL_H
#define TCP_QUEUE_TO_UDP_THREAD_POOL_H

#include <vector>
class TcpQueueToUdpThreadPool {
public:
    void run();
private:
    void processData();
    void sendDataViaUdp(int socket, std::shared_ptr<std::vector<char>> data);
};

#endif // TCP_QUEUE_TO_UDP_THREAD_POOL_H
