#include "SocketManager.h"
#include <iostream>
#include <thread>
#include <exception>
#include <Log.h>

using namespace Logger;

SocketManager::SocketManager(int udpPort, const std::vector<std::pair<std::string, int>>& peerConnections)
    : localUdpSocket(udpPort) {
    for (const auto& connection : peerConnections) {
        peerTcpSockets.emplace_back(connection.first, connection.second);
    }
}

SocketManager::~SocketManager() {
    cleanupResources();
}

void SocketManager::manageSockets() {
    bool running = true;

    // LocalHostReadThread
    std::thread localHostReadThread(&SocketManager::localHostReadTask, this, std::ref(running));

    // LocalHostWriteThread
    std::thread localHostWriteThread(&SocketManager::localHostWriteTask, this, std::ref(running));

    // PeerHostReadThreads
    std::vector<std::thread> peerHostReadThreads;
    for (auto& peerTcpSocket : peerTcpSockets) {
        peerHostReadThreads.emplace_back(&SocketManager::peerHostReadTask, this, std::ref(running), std::ref(peerTcpSocket));
    }

    // PeerHostWriteThreads
    std::vector<std::thread> peerHostWriteThreads;
    for (auto& peerTcpSocket : peerTcpSockets) {
        peerHostWriteThreads.emplace_back(&SocketManager::peerHostWriteTask, this, std::ref(running), std::ref(peerTcpSocket));
    }

    // Join threads
    localHostReadThread.join();
    localHostWriteThread.join();
    for (auto& thread : peerHostReadThreads) {
        thread.join();
    }
    for (auto& thread : peerHostWriteThreads) {
        thread.join();
    }
}

void SocketManager::localHostReadTask(bool& running) {
    try {
        while (running) {
            std::vector<char> data = localUdpSocket.receive();
            if(data.size() != 0)
            {
                udpToTcpQueue.enqueue(data);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": LocalHostReadThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::localHostWriteTask(bool& running) {
    int i = 0;
    try {
        while (running) {
            std::vector<char> data = udpToTcpQueue.dequeue();
            if(data.size())
            {
                peerTcpSockets[i].send(data);
                i = (i + 1) % peerTcpSockets.size();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": LocalHostWriteThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::peerHostReadTask(bool& running, PeerTcpSocket& peerTcpSocket) {
    try {
        peerTcpSocket.sendHandshake();
        while (running) {
            std::vector<char> data = peerTcpSocket.receive();
            if(data.size())
            {
                tcpToUdpQueue.enqueue(data);
            }
            else
            {
                Log::getInstance().error("SocketManager: Empty data received.");
                running = false;
                this->udpToTcpQueue.cancel();
                this->tcpToUdpQueue.cancel();
                this->localUdpSocket.close();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": PeerHostReadThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::peerHostWriteTask(bool& running, PeerTcpSocket& peerTcpSocket) {
    try {
        while (running) {
            std::vector<char> data = tcpToUdpQueue.dequeue();
            if(data.size())
            {
                localUdpSocket.send(data);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": PeerHostWriteThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::cleanupResources() {
    // Clean up resources
}
