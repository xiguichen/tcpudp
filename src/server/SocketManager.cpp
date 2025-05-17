#include "SocketManager.h"
#include <iostream>
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif
#include "TcpToUdpSocketMap.h"
#include "UdpToTcpSocketMap.h"
#include "TcpToQueueThread.h"
#include "UdpToQueueThread.h"
#include "UdpQueueToTcpThreadPool.h"
#include "TcpQueueToUdpThreadPool.h"
#include <Socket.h>
#include <Log.h>
using namespace Logger;

SocketManager::SocketManager() : serverSocket(-1) {}

SocketManager::~SocketManager() {

    Log::getInstance().info("Cleaning up resources");
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    if (serverSocket != -1) {
        SocketClose(serverSocket);
    }
}

void SocketManager::createSocket() {
    // Use the new CreateSocket function
    serverSocket = CreateSocket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // Set socket to non-blocking mode
    if (SetSocketNonBlocking(serverSocket) < 0) {
        std::cerr << "Failed to set socket to non-blocking mode" << std::endl;
        SocketClose(serverSocket);
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options" << std::endl;
        SocketClose(serverSocket);
        exit(EXIT_FAILURE);
    }
    
    Log::getInstance().info("Created non-blocking server socket");
}

void SocketManager::bindToPort(int port) {
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind to port " << port << std::endl;
        SocketClose(serverSocket);
        exit(EXIT_FAILURE);
    }
    
    Log::getInstance().info(std::format("Server socket bound to port {}", port));
}

void SocketManager::listenForConnections() {
    if (SocketListen(serverSocket, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen for connections" << std::endl;
        SocketClose(serverSocket);
        exit(EXIT_FAILURE);
    }
    
    Log::getInstance().info("Server socket listening for connections");
}

void SocketManager::acceptConnection() {
    // Check if server socket is readable with a short timeout
    if (!IsSocketReadable(serverSocket, 100)) { // 100ms timeout
        // No pending connections, yield to other threads
        std::this_thread::yield();
        return;
    }
    
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    
    // Accept connection non-blocking
    int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
    
#ifdef _WIN32
    if (clientSocket == INVALID_SOCKET) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            // No pending connections, just return
            return;
        }
        std::cerr << "Failed to accept connection, error: " << error << std::endl;
        return;
    }
#else
    if (clientSocket < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No pending connections, just return
            return;
        }
        std::cerr << "Failed to accept connection, error: " << strerror(errno) << std::endl;
        return;
    }
#endif

    // Set client socket to non-blocking mode
    if (SetSocketNonBlocking(clientSocket) < 0) {
        std::cerr << "Failed to set client socket to non-blocking mode" << std::endl;
        SocketClose(clientSocket);
        return;
    }

    // Create a new UDP socket for this client
    int udpSocket = CreateSocket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket < 0) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        SocketClose(clientSocket);
        return;
    }
    
    // Set UDP socket to non-blocking mode
    if (SetSocketNonBlocking(udpSocket) < 0) {
        std::cerr << "Failed to set UDP socket to non-blocking mode" << std::endl;
        SocketClose(clientSocket);
        SocketClose(udpSocket);
        return;
    }

    // Map the TCP and UDP sockets
    UdpToTcpSocketMap::getInstance().mapSockets(udpSocket, clientSocket);
    TcpToUdpSocketMap::getInstance().mapSockets(clientSocket, udpSocket);
    
    // Log client connection
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
    Log::getInstance().info(std::format("Accepted connection from {}:{}", clientIP, ntohs(clientAddr.sin_port)));

    // Start threads for handling TCP and UDP data
    startTcpToQueueThread(clientSocket);
    startUdpToQueueThread(udpSocket);
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
void SocketManager::startTcpQueueToUdpThreadPool() {
  for (int i = 0; i < 5; ++i) {
      auto thread = std::thread([]() {
          // Assuming TcpQueueToUdpThread is a class that handles the processing
          TcpQueueToUdpThreadPool tcpQueueToUdpThreadPool;
          tcpQueueToUdpThreadPool.run();
      });
      threads.push_back(std::move(thread));
  }
}

void SocketManager::startUdpQueueToTcpThreadPool() {
    for (int i = 0; i < 1; ++i) {
        auto thread = std::thread([]() {
            // Assuming UdpQueueToTcpThread is a class that handles the processing
            UdpQueueToTcpThreadPool udpQueueToTcpThreadPool;
            udpQueueToTcpThreadPool.run();
        });
        threads.push_back(std::move(thread));
    }
}

