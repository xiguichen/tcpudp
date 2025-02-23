#include "TcpQueueToUdpThreadPool.h"
#include "TcpDataQueue.h"
#include "Configuration.h"
#include "TcpToUdpSocketMap.h"
#include <iostream>
#include <vector>
#include <memory>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include "TcpQueueToUdpThreadPool.h"

void TcpQueueToUdpThreadPool::run() {
    while (true) {
        processData();
    }
}

void TcpQueueToUdpThreadPool::processData() {
    auto dataPair = TcpDataQueue::getInstance().dequeue();
    if (dataPair.second) {
        sendDataViaUdp(dataPair.first, dataPair.second);
    }
}

void TcpQueueToUdpThreadPool::sendDataViaUdp(int socket, std::shared_ptr<std::vector<char>> data) {

    auto udpSocket = TcpToUdpSocketMap::getInstance().retrieveMappedUdpSocket(socket);
    
    sockaddr_in udpAddr;
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port = htons(Configuration::getInstance()->getPortNumber());
    inet_pton(AF_INET, Configuration::getInstance()->getSocketAddress().c_str(), &udpAddr.sin_addr);

    ssize_t sentBytes = sendto(udpSocket, data->data(), data->size(), 0, (struct sockaddr*)&udpAddr, sizeof(udpAddr));
    if (sentBytes < 0) {
        std::cerr << "Failed to send data via UDP" << std::endl;
    }
}
