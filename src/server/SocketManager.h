#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H

#include <thread>
#include <Socket.h>
#include <vector>

class SocketManager {
public:
    SocketManager();
    ~SocketManager();

    void createSocket();
    void bindToPort(int port);
    void listenForConnections();
    void acceptConnection();
    void startTcpToQueueThread(int clientSocket);
    void startUdpToQueueThread(int clientSocket);
    void startTcpQueueToUdpThreadPool();
    void startUdpQueueToTcpThreadPool();

  private:
    int serverSocket;
    std::vector<std::thread> threads;
};

#endif // SOCKET_MANAGER_H
