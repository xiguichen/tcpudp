#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H

#include <thread>
#include <Socket.h>
#include <vector>
#include <atomic>

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
    
    // Shutdown mechanism
    void shutdown();
    bool isRunning() const;
    
    // Static method to check if server is running
    static bool isServerRunning();

  private:
    int serverSocket;
    std::vector<std::thread> threads;
    
    // Static atomic flag for controlling all threads
    static std::atomic<bool> s_running;
    
    // Prevent threads from joining multiple times during cleanup
    bool threadsJoined;
};

#endif // SOCKET_MANAGER_H
