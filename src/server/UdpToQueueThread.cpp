#include "UdpToQueueThread.h"

#include <iostream>
#include <netinet/in.h>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>
#include "UdpDataQueue.h"

void UdpToQueueThread::run() {
    while (true) {
        char buffer[1024];
        ssize_t bytesRead = readFromUdpSocket(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            enqueueData(buffer, bytesRead);
        }
    }
}

size_t UdpToQueueThread::readFromUdpSocket(char* buffer, size_t bufferSize) {
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    ssize_t bytesRead = recvfrom(socket_, buffer, bufferSize, 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (bytesRead > 0) {
        std::cout << "Received data: " << std::string(buffer, bytesRead) << std::endl;
        
    } else {
        std::cerr << "Error reading from UDP socket" << std::endl;
    }
return bytesRead;
}

void UdpToQueueThread::enqueueData(char* data, size_t length) {
    auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
    UdpDataQueue::getInstance().enqueue(socket_, dataVector);
    std::cout << "Data enqueued" << std::endl;

}
