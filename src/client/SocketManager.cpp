#include "SocketManager.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <exception>

SocketManager::SocketManager(int udpPort, int tcpPort, const std::string& address)
    : localUdpSocket(udpPort), peerTcpSocket(address, tcpPort) {}

SocketManager::~SocketManager() {
    cleanupResources();
}

void SocketManager::manageSockets() {
    std::mutex mtx;
    bool running = true;

    // LocalHostReadThread
    std::thread localHostReadThread([&]() {
        try {
            while (running) {
                std::vector<char> data = localUdpSocket.receive();
                if(data.size() != 0)
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    std::cout << "Received data: length" << data.size() << std::endl;
                    std::cout << "Received data: " << std::string(data.begin(), data.end()) << std::endl;
                    udpToTcpQueue.enqueue(data);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "LocalHostReadThread error: " << e.what() << std::endl;
            running = false;
        }
    });

    // LocalHostWriteThread
    std::thread localHostWriteThread([&]() {
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
            std::cerr << "LocalHostWriteThread error: " << e.what() << std::endl;
            running = false;
        }
    });

    // PeerHostReadThread
    std::thread peerHostReadThread([&]() {
        try {
            while (running) {
                std::vector<char> data = peerTcpSocket.receive();
                if(data.size())
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    tcpToUdpQueue.enqueue(data);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "PeerHostReadThread error: " << e.what() << std::endl;
            running = false;
        }
    });

    // PeerHostWriteThread
    std::thread peerHostWriteThread([&]() {
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
            std::cerr << "PeerHostWriteThread error: " << e.what() << std::endl;
            running = false;
        }
    });

    // Join threads
    localHostReadThread.join();
    localHostWriteThread.join();
    peerHostReadThread.join();
    peerHostWriteThread.join();
}

void SocketManager::cleanupResources() {
    // Clean up resources
}
