#include "UdpToQueueThread.h"

#include <iostream>
#include <Socket.h>
#include <vector>
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "UdpDataQueue.h"
#include <Socket.h>
#include <Log.h>

using namespace Logger;

void UdpToQueueThread::run() {
    // Set socket to non-blocking mode
    SetSocketNonBlocking(socket_);
    
    const int bufferSize = 4096;
    char buffer[bufferSize];
    
    while (true) {
        // Check if socket is readable with a short timeout
        if (IsSocketReadable(socket_, 100)) { // 100ms timeout
            ssize_t bytesRead = readFromUdpSocket(buffer, sizeof(buffer));
            if (bytesRead > 0) {
                enqueueData(buffer, bytesRead);
            } else if (bytesRead == SOCKET_ERROR_WOULD_BLOCK || bytesRead == SOCKET_ERROR_TIMEOUT) {
                // No data available or timeout, just continue
                std::this_thread::yield();
            }
        } else {
            // No data available, yield to other threads
            std::this_thread::yield();
        }
    }
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

void UdpToQueueThread::enqueueData(char* data, size_t length) {
    auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
    Log::getInstance().info(std::format("UDP -> Queue: Data Enqueue, Length: {}", dataVector->size()));
    UdpDataQueue::getInstance().enqueue(socket_, dataVector);
}
