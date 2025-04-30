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
    while (true) {
        char buffer[4096];
        ssize_t bytesRead = readFromUdpSocket(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            enqueueData(buffer, bytesRead);
        }
    }
}

size_t UdpToQueueThread::readFromUdpSocket(char* buffer, size_t bufferSize) {
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    Log::getInstance().info("Server -> Server (UDP Data)");
    ssize_t bytesRead = RecvUdpData(socket_, buffer, bufferSize, 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (bytesRead > 0) {
    } else {
        std::cerr << "Error reading from UDP socket" << std::endl;
    }
return bytesRead;
}

void UdpToQueueThread::enqueueData(char* data, size_t length) {
    auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
    Log::getInstance().info(std::format("UDP -> Queue: Data Enqueue, Length: {}", dataVector->size()));
    UdpDataQueue::getInstance().enqueue(socket_, dataVector);
}
