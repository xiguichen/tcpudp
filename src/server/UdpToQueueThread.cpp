#include "UdpToQueueThread.h"

#include <iostream>
#include <netinet/in.h>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

void UdpToQueueThread::run() {
    while (true) {
        readFromUDPSocket();
        enqueueData();
    }
}

void UdpToQueueThread::readFromUDPSocket() {
    char buffer[1024];
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    ssize_t bytesRead = recvfrom(socket_, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (bytesRead > 0) {
        std::cout << "Received data: " << std::string(buffer, bytesRead) << std::endl;
        // Store the data for enqueuing
        // This is a placeholder for actual data handling
    } else {
        std::cerr << "Error reading from UDP socket" << std::endl;
    }
}

void UdpToQueueThread::enqueueData() {
    // Placeholder for enqueuing data logic
    std::cout << "Data enqueued" << std::endl;
}
