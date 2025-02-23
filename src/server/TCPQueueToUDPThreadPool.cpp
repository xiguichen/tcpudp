
#include "TcpQueueToUdpThreadPool.h"
#include "TcpDataQueue.h"
#include "Configuration.h"
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
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

void TcpQueueToUdpThreadPool::sendDataViaUdp() {
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return;
        }
    #endif
    
        SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return;
    }

    sockaddr_in udpAddr;
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port = htons(Configuration::getInstance()->getPortNumber());
    inet_pton(AF_INET, Configuration::getInstance()->getSocketAddress().c_str(), &udpAddr.sin_addr);

    ssize_t sentBytes = sendto(sockfd, data->data(), data->size(), 0, (struct sockaddr*)&udpAddr, sizeof(udpAddr));
    if (sentBytes < 0) {
        std::cerr << "Failed to send data via UDP" << std::endl;
    }

    #ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
    #else
        close(sockfd);
    #endif
}
