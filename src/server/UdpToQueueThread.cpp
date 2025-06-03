#include "UdpToQueueThread.h"

#include <iostream>
#include <Socket.h>
#include "SocketManager.h"
#include <vector>
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <Socket.h>
#include <Log.h>

using namespace Logger;

void UdpToQueueThread::run() {
    // Set socket to non-blocking mode
    SetSocketNonBlocking(socket_);
    
    // Initialize memory monitoring
    MemoryMonitor::getInstance().start();
    
    // Get a buffer from the memory pool with optimal size for UDP packets
    auto buffer = MemoryPool::getInstance().getBuffer(4096);
    
    while (SocketManager::isServerRunning()) {
        // Check if socket is readable with a short timeout
        if (IsSocketReadable(socket_, 100)) { // 100ms timeout
            ssize_t bytesRead = readFromUdpSocket(buffer->data(), buffer->capacity());
            if (bytesRead > 0) {
                // Resize the buffer to the actual data size
                buffer->resize(bytesRead);
                
                // Enqueue the data and get a new buffer
                enqueueData(buffer);
                
                // Get a new buffer for the next read
                buffer = MemoryPool::getInstance().getBuffer(4096);
            } else if (bytesRead == SOCKET_ERROR_WOULD_BLOCK || bytesRead == SOCKET_ERROR_TIMEOUT) {
                // No data available or timeout, just continue
                std::this_thread::yield();
            }
        } else {
            // No data available, yield to other threads
            std::this_thread::yield();
        }
        
        // Periodically log memory usage (every ~1000 iterations)
        static int counter = 0;
        if (++counter % 1000 == 0) {
            MemoryMonitor::getInstance().logMemoryUsage();
        }
    }
    
    // Recycle the buffer when the thread exits
    MemoryPool::getInstance().recycleBuffer(buffer);
    Log::getInstance().info("UDP to Queue thread exiting");
}

size_t UdpToQueueThread::readFromUdpSocket(char* buffer, size_t bufferSize) {
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    Log::getInstance().info("Server -> Server (UDP Data)");
    
    // Use non-blocking receive with timeout
    ssize_t bytesRead = RecvUdpDataNonBlocking(socket_, buffer, bufferSize, 0, 
                                              (struct sockaddr*)&clientAddr, &clientAddrLen, 1000); // 1 second timeout
    
    if (bytesRead > 0) {
        // Data received successfully
        MemoryMonitor::getInstance().trackAllocation(bytesRead);
    } else if (bytesRead == SOCKET_ERROR_TIMEOUT) {
        Log::getInstance().info("UDP socket read timeout");
    } else if (bytesRead == SOCKET_ERROR_WOULD_BLOCK) {
        Log::getInstance().info("UDP socket would block");
    } else {
        std::cerr << "Error reading from UDP socket: " << bytesRead << std::endl;
        SocketLogLastError();
    }
    
    return bytesRead;
}

void UdpToQueueThread::enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer) {

    Log::getInstance().info(std::format("UDP -> Queue: Data Enqueue, Length: {}", dataBuffer->size()));
    
    // Enqueue the data buffer directly (no need to copy)
    queue->enqueue(dataBuffer);
}
