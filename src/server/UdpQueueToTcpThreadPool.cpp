#include "UdpQueueToTcpThreadPool.h"

#include "UdpDataQueue.h"
#include "UdpToTcpSocketMap.h"
#include <sys/socket.h>
#include <thread>
#include <iostream>

void UdpQueueToTcpThreadPool::run() {
    // Start a thread to process data concurrently
    std::thread processingThread(&UdpQueueToTcpThreadPool::processDataConcurrently, this);
    processingThread.detach(); // Detach the thread to run independently
}

void UdpQueueToTcpThreadPool::processDataConcurrently() {
    while (true) {
        // Dequeue data from the UDP data queue
        auto dataPair = UdpDataQueue::getInstance().dequeue();
        if (dataPair.first != -1) {
            // Extract socket and data
            int udpSocket = dataPair.first;
            auto data = dataPair.second;

            // Retrieve mapped TCP socket
            int tcpSocket = UdpToTcpSocketMap::getInstance().retrieveMappedTcpSocket(udpSocket);

            // Send data via TCP
            sendDataViaTcp(tcpSocket, data);
        }
    }
}

void UdpQueueToTcpThreadPool::sendDataViaTcp(int tcpSocket, const std::shared_ptr<std::vector<char>>& data) {
    if (tcpSocket != -1) {
        // Send data length first
        int length = htonl(data->size());
        send(tcpSocket, &length, sizeof(length), 0);

        // Send the actual data
        send(tcpSocket, data->data(), length, 0);

        std::cout << "Data sent via TCP" << std::endl;
    } else {
        std::cerr << "Error: Invalid TCP socket" << std::endl;
    }
}
