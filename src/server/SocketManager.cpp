#include "SocketManager.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "TCPToUDPSocketMap.h"
#include "UDPToTCPSocketMap.h"
#include "TCPToQueueThread.h"
#include "UdpToQueueThread.h"

SocketManager::SocketManager() : serverSocket(-1) {}

SocketManager::~SocketManager() {
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    if (serverSocket != -1) {
        close(serverSocket);
    }
}

void SocketManager::createSocket() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        exit(EXIT_FAILURE);
    }
}

void SocketManager::bindToPort(int port) {
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind to port " << port << std::endl;
        exit(EXIT_FAILURE);
    }
}

void SocketManager::listenForConnections() {
    if (listen(serverSocket, 5) < 0) {
        std::cerr << "Failed to listen for connections" << std::endl;
        exit(EXIT_FAILURE);
    }
}

void SocketManager::acceptConnection() {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
    if (clientSocket < 0) {
        std::cerr << "Failed to accept connection" << std::endl;
        return;
    }

    // Create a new UDP socket for this client
    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket < 0) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        close(clientSocket);
        return;
    }

    // Map the TCP and UDP sockets
    UDPToTCPSocketMap::getInstance().mapSockets(udpSocket, clientSocket);
    TCPToUDPSocketMap::getInstance().mapSockets(clientSocket, udpSocket);

    // Start threads for handling TCP and UDP data
    startTcpToQueueThread(clientSocket);
    startUdpToQueueThread(clientSocket);
}



void SocketManager::startTcpToQueueThread(int clientSocket) {

    auto thread = std::thread([clientSocket]() {
        TcpToQueueThread tcpToQueueThread(clientSocket);
        tcpToQueueThread.run();
    });
    threads.push_back(std::move(thread));
}

void SocketManager::startUdpToQueueThread(int clientSocket) {
    auto thread = std::thread([clientSocket]() {
        UdpToQueueThread udpToQueueThread(clientSocket);
        udpToQueueThread.run();
    });
    threads.push_back(std::move(thread));
}
