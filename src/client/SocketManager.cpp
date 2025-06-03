#include "SocketManager.h"
#include <iostream>
#include <thread>
#include <exception>
#include <Log.h>
#include <Protocol.h>
#include <MemoryPool.h>

using namespace Logger;

SocketManager::SocketManager(int udpPort, const std::vector<std::pair<std::string, int>>& peerConnections, uint32_t clientId)
    : localUdpSocket(udpPort) {
    for (const auto& connection : peerConnections) {
        peerTcpSockets.emplace_back(std::make_shared<PeerTcpSocket>(connection.first, connection.second, clientId));
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
        peerHostReadThreads.emplace_back(&SocketManager::peerHostReadTask, this, std::ref(running), std::ref(*peerTcpSocket));
    }

    // PeerHostWriteThreads
    std::vector<std::thread> peerHostWriteThreads;
    for (auto& peerTcpSocket : peerTcpSockets) {
        peerHostWriteThreads.emplace_back(&SocketManager::peerHostWriteTask, this, std::ref(running), std::ref(*peerTcpSocket));
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
        Log::getInstance().info("Starting LocalHostReadThread");
        while (running) {
            // Non-blocking receive with short timeout
            std::vector<char> data = localUdpSocket.receive();
            if(data.size() != 0) {
                auto dataPtr = std::make_shared<std::vector<char>>(std::move(data));
                udpToTcpQueue.enqueue(dataPtr);
            } else {
                // No data available, yield to other threads
                std::this_thread::yield();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": LocalHostReadThread error: " << e.what() << std::endl;
        Log::getInstance().error(std::format("LocalHostReadThread error: {}", e.what()));
        running = false;
    }
}

void SocketManager::localHostWriteTask(bool& running) {
    int i = 0;
    uint8_t msgId = 0;
    try {
        while (running) {

            // Dequeue data with timeout
            auto data = udpToTcpQueue.dequeue();
            try {
                Log::getInstance().info(std::format("send tcp via connection {}", i));
                // Only send if the socket is authenticated
                if (peerTcpSockets[i]->isAuthenticated()) {
                    // Send data with non-blocking I/O
                    auto outputData = MemoryPool::getInstance().getBuffer(data->size() + 20);
                    outputData->resize(0);
                    Log::getInstance().info(std::format("send msg {}", msgId));
                    UvtUtils::AppendUdpData(*data, msgId++, *outputData);
                    peerTcpSockets[i]->send(*outputData);
                } else {
                    Log::getInstance().warning(std::format("Socket {} not authenticated, skipping send", i));
                }
                i = (i + 1) % peerTcpSockets.size();
            } 
            catch (const std::exception& e) {
                Log::getInstance().error(std::format("Failed to send data to peer: {}", e.what()));
                // Continue with next peer if one fails
                i = (i + 1) % peerTcpSockets.size();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": LocalHostWriteThread error: " << e.what() << std::endl;
        Log::getInstance().error(std::format("LocalHostWriteThread error: {}", e.what()));
        running = false;
    }
}

void SocketManager::peerHostReadTask(bool& running, PeerTcpSocket& peerTcpSocket) {
    try {
        // Complete the handshake process (send handshake and receive response)
        peerTcpSocket.completeHandshake();
        
        int emptyDataCount = 0;
        const int MAX_EMPTY_DATA_COUNT = 1000; // Allow some empty receives before considering connection dead
        
        while (running) {
            // Receive data with non-blocking I/O
            auto data = peerTcpSocket.receive();
            
            if(data->size()) {
                // Reset empty data counter on successful receive
                emptyDataCount = 0;
                tcpToUdpQueue.enqueue(data);
            } else {
                // Count consecutive empty receives
                emptyDataCount++;
                
                if (emptyDataCount > MAX_EMPTY_DATA_COUNT) {
                    Log::getInstance().error("SocketManager: Too many empty data receives, connection may be dead");
                    running = false;
                    this->localUdpSocket.close();
                } else {
                    // No data available, yield to other threads
                    std::this_thread::yield();
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": PeerHostReadThread error: " << e.what() << std::endl;
        Log::getInstance().error(std::format("PeerHostReadThread error: {}", e.what()));
        running = false;
    }
}

void SocketManager::peerHostWriteTask(bool& running, PeerTcpSocket& peerTcpSocket) {
    try {
        while (running) {
            // Dequeue data with timeout
            auto data = tcpToUdpQueue.dequeue();
            localUdpSocket.send(*data);
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": PeerHostWriteThread error: " << e.what() << std::endl;
        Log::getInstance().error(std::format("PeerHostWriteThread error: {}", e.what()));
        running = false;
    }
}

void SocketManager::cleanupResources() {
    // Clean up resources
}
