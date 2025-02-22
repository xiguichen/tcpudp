#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H

#include <thread>
#include <map>
#include <netinet/in.h>
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

private:
    int serverSocket;
    std::vector<std::thread> threads;
};

#endif // SOCKET_MANAGER_H
