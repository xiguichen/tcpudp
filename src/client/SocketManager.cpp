#include "SocketManager.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <exception>
#include <Log.h>

using namespace Logger;

SocketManager::SocketManager(int udpPort, int tcpPort, const std::string& address)
    : localUdpSocket(udpPort), peerTcpSocket(address, tcpPort) {}

SocketManager::~SocketManager() {
    cleanupResources();
}

void SocketManager::manageSockets() {
    std::mutex mtx;
    bool localHostReadRunning = true;
    bool localHostWriteRunning = true;
    bool peerHostReadRunning = true;
    bool peerHostWriteRunning = true;

    // LocalHostReadThread
    std::thread localHostReadThread(&SocketManager::localHostReadTask, this, std::ref(mtx), std::ref(localHostReadRunning));

    // LocalHostWriteThread
    std::thread localHostWriteThread(&SocketManager::localHostWriteTask, this, std::ref(mtx), std::ref(localHostWriteRunning));

    // PeerHostReadThread
    std::thread peerHostReadThread(&SocketManager::peerHostReadTask, this, std::ref(mtx), std::ref(peerHostReadRunning));

    // PeerHostWriteThread
    std::thread peerHostWriteThread(&SocketManager::peerHostWriteTask, this, std::ref(mtx), std::ref(peerHostWriteRunning));

    // Join threads
    localHostReadThread.join();
    localHostWriteThread.join();
    peerHostReadThread.join();
    peerHostWriteThread.join();
}

void SocketManager::localHostReadTask(std::mutex& mtx, bool& running) {
    try {
        while (running) {
            std::vector<char> data = localUdpSocket.receive();
            if(data.size() != 0)
            {
                std::lock_guard<std::mutex> lock(mtx);
                std::cout << "localHostReadThread" << ": Received data: length " << data.size() << std::endl;
                udpToTcpQueue.enqueue(data);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": LocalHostReadThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::localHostWriteTask(std::mutex& mtx, bool& running) {
    try {
        while (running) {
            std::vector<char> data = udpToTcpQueue.dequeue();
            if(data.size())
            {
                std::lock_guard<std::mutex> lock(mtx);
                peerTcpSocket.send(data);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": LocalHostWriteThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::peerHostReadTask(std::mutex& mtx, bool& running) {
    try {
        while (running) {
            std::vector<char> data = peerTcpSocket.receive();
            if(data.size())
            {
                std::lock_guard<std::mutex> lock(mtx);
                tcpToUdpQueue.enqueue(data);
            }
            else
            {
                Log::getInstance().error("SocketManager: Empty data received.");
                running = false;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": PeerHostReadThread error: " << e.what() << std::endl;
        running = false;
    }
}

void SocketManager::peerHostWriteTask(std::mutex& mtx, bool& running) {
    try {
        while (running) {
            std::vector<char> data = tcpToUdpQueue.dequeue();
            if(data.size())
            {
                std::lock_guard<std::mutex> lock(mtx);
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
