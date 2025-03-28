#include "SocketManager.h"
#include <iostream>
#include <thread>
#include <exception>
#include <Log.h>

using namespace Logger;

SocketManager::SocketManager(int udpPort, int tcpPort, const std::string& address)
    : localUdpSocket(udpPort), peerTcpSocket(address, tcpPort) {}

SocketManager::~SocketManager() {
    cleanupResources();
}

void SocketManager::manageSockets() {
    bool running = true;

    // LocalHostReadThread
    std::thread localHostReadThread(&SocketManager::localHostReadTask, this, std::ref(running));

    // LocalHostWriteThread
    std::thread localHostWriteThread(&SocketManager::localHostWriteTask, this, std::ref(running));

    // PeerHostReadThread
    std::thread peerHostReadThread(&SocketManager::peerHostReadTask, this, std::ref(running));

    // PeerHostWriteThread
    std::thread peerHostWriteThread(&SocketManager::peerHostWriteTask, this, std::ref(running));

    // Join threads
    localHostReadThread.join();
    localHostWriteThread.join();
    peerHostReadThread.join();
    peerHostWriteThread.join();
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
    try {
        while (running) {
            std::vector<char> data = udpToTcpQueue.dequeue();
            if(data.size())
            {
                peerTcpSocket.send(data);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": LocalHostWriteThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::peerHostReadTask(bool& running) {
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

void SocketManager::peerHostWriteTask(bool& running) {
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
