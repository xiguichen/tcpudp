#pragma once

#include <thread>
#include <Socket.h>
#include <vector>
#include <atomic>
#include "BlockingQueue.h"

class SocketManager {
public:
    SocketManager();
    ~SocketManager();

    void createSocket();
    void bindToPort(int port);
    void listenForConnections();
    void acceptConnection();
    void startTcpToQueueThread(SocketFd clientSocket, SocketFd udpSocket, uint32_t clientId);
    void startUdpToQueueThread(int clientSocket, std::shared_ptr<BlockingQueue>& queue);
    void startTcpQueueToUdpThread(SocketFd udpSocket, std::shared_ptr<BlockingQueue>& queue);
    void StartUdpQueueToTcpThread(int clientSocket, std::shared_ptr<BlockingQueue>& queue)
    {


    }
    bool CapabilityNegotiate(SocketFd socket, uint32_t& clientId, SocketFd& udpSocket);

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

