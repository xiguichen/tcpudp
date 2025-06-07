#include "SocketManager.h"
#include <iostream>
#include <future>  // for std::async
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif
#include "TcpToQueueThread.h"
#include "UdpToQueueThread.h"
#include "UdpQueueToTcpThread.h"
#include "TcpQueueToUdpThread.h"
#include <Protocol.h>
#include <Socket.h>
#include <Log.h>
#include "Configuration.h"
#include "ClientUdpSocketManager.h"
#include "QueueManager.h"
using namespace Logger;

// Static variable for running state
std::atomic<bool> SocketManager::s_running(true);

SocketManager::SocketManager() : serverSocket(-1), threadsJoined(false) {}

SocketManager::~SocketManager() {
    // Call shutdown to ensure proper cleanup
    shutdown();
}

void SocketManager::shutdown() {
    // First log the shutdown attempt
    Log::getInstance().info("Shutting down server...");
    
    // Set running flag to false to stop all threads
    s_running.store(false);
    
    // Wait a short time for threads to notice the running flag change
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Close server socket first to prevent accepting new connections
    if (serverSocket != -1) {
        Log::getInstance().info("Closing server socket");
        SocketClose(serverSocket);
        serverSocket = -1;
    }
    
    // Only join threads if they haven't been joined already
    if (!threadsJoined) {
        Log::getInstance().info("Waiting for all threads to complete...");
        
        // Join each thread with a timeout
        for (auto& thread : threads) {
            if (thread.joinable()) {
                try {
                    // Create a future to track if the thread join completes
                    std::future<void> future = std::async(std::launch::async, [&thread]() {
                        try {
                            if (thread.joinable()) {
                                thread.join();
                            }
                        } catch (const std::exception& e) {
                            Log::getInstance().error(std::string("Exception in thread join: ") + e.what());
                        }
                    });
                    
                    // Wait for thread join with timeout
                    auto status = future.wait_for(std::chrono::milliseconds(500)); // 500ms timeout
                    
                    if (status == std::future_status::timeout) {
                        // Thread join timed out
                        Log::getInstance().warning("Thread join timed out, detaching");
                        thread.detach(); // Detach the original thread
                    }
                } catch (const std::exception& e) {
                    Log::getInstance().error(std::string("Exception joining thread: ") + e.what());
                }
            }
        }
        
        // Clear the thread vector
        threads.clear();
        threadsJoined = true;
        Log::getInstance().info("All threads processed");
    }
    
    Log::getInstance().info("Server shutdown complete");
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

    auto config = Configuration::getInstance();

    // for each allowed client id
    for (const auto& clientId : config->getAllowedClientIds()) {
        // Create a UDP socket for this client
        SocketFd udpSocket = ClientUdpSocketManager::getInstance().getOrCreateUdpSocket(clientId);
        if (udpSocket < 0) {
            std::cerr << "Failed to create UDP socket for client ID: " << clientId << std::endl;
            continue; // Skip this client if UDP socket creation failed
        }
        
        // Log the creation of the UDP socket
        Log::getInstance().info(std::format("Created UDP queue for client ID: {}", clientId));

        auto udpToTcpQueue =  QueueManager::getInstance().getUdpToTcpQueueForClient(clientId);

        // Start the udp to queue thread for this client
        startUdpToQueueThread(udpSocket, *udpToTcpQueue);

    }


    
    Log::getInstance().info("Server socket listening for connections");
}

bool SocketManager::isRunning() const {
    return s_running.load();
}

bool SocketManager::isServerRunning() {
    return s_running.load();
}

void SocketManager::acceptConnection() {
    // Check if server should still be running
    if (!isServerRunning()) {
        return;
    }
    
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

    // Log client connection
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
    Log::getInstance().info(std::format("Accepted connection from {}:{}", clientIP, ntohs(clientAddr.sin_port)));

    uint32_t clientId;
    SocketFd udpSocket;
    if(this->CapabilityNegotiate(clientSocket, clientId, udpSocket))
    {
        auto tcpToUdpQueue =  QueueManager::getInstance().getTcpToUdpQueueForClient(clientId);
        auto udpToTcpQueue =  QueueManager::getInstance().getUdpToTcpQueueForClient(clientId);

        // TCP to UDP
        startTcpToQueueThread(clientSocket, udpSocket, clientId);

        Log::getInstance().info(std::format("Starting TcpQueueToUdpThread for client ID: {}, udpSocket: {}", clientId, udpSocket));
        startTcpQueueToUdpThread(udpSocket, *tcpToUdpQueue);

        // UDP to TCP
        startUdpQueueToTcpThread(clientSocket, *udpToTcpQueue);
    }
}



void SocketManager::startTcpToQueueThread(SocketFd clientSocket, SocketFd udpSocket, uint32_t clientId) {

    auto thread = std::thread([clientSocket, udpSocket, clientId]() {
        TcpToQueueThread tcpToQueueThread(clientSocket, udpSocket, clientId);
        tcpToQueueThread.run();
    });
    threads.push_back(std::move(thread));
}

void SocketManager::startUdpToQueueThread(SocketFd clientSocket, BlockingQueue& queue) {
    auto thread = std::thread([clientSocket, &queue]() {
        UdpToQueueThread udpToQueueThread(clientSocket, queue);
        udpToQueueThread.run();
    });
    threads.push_back(std::move(thread));
}


void SocketManager::startUdpQueueToTcpThread(SocketFd clientSocket, BlockingQueue &queue)
{
    auto thread = std::thread([clientSocket, &queue]() {
        UdpQueueToTcpThread udpQueueToTcpThread(clientSocket, queue);
        udpQueueToTcpThread.run();
    });
    threads.push_back(std::move(thread));
}

void SocketManager::startTcpQueueToUdpThread(SocketFd udpSocket, BlockingQueue& queue) {

    Log::getInstance().info(std::format("Starting TcpQueueToUdpThread, socket: {}", udpSocket));
    auto thread = std::thread([udpSocket, &queue]() {
        // Assuming TcpQueueToUdpThread is a class that handles the processing
        Log::getInstance().info(std::format("TcpQueueToUdpThread started for UDP socket: {}", udpSocket));
        TcpQueueToUdpThread tcpQueueToUdpThread(udpSocket, queue);
        tcpQueueToUdpThread.run();
    });
    threads.push_back(std::move(thread));
}

bool SocketManager::CapabilityNegotiate(SocketFd tcpSocket, uint32_t& clientId, SocketFd& udpSocket)
{

  // Get a buffer from the memory pool with optimal size for TCP packets
  const int bufferSize = 65535;
  auto buffer = MemoryPool::getInstance().getBuffer(bufferSize);
  
  // Set socket to non-blocking mode
  SetSocketNonBlocking(tcpSocket);
  
  // Wait for socket to be readable with timeout
  if (!IsSocketReadable(tcpSocket, 2000)) { // 2 seconds timeout
    Log::getInstance().error("Socket not readable within timeout");
    SocketClose(tcpSocket);
    return false;
  }

  Log::getInstance().info("Begin capability negotiate");

  // Receive handshake data with timeout
  int result = RecvTcpDataWithSizeNonBlocking(tcpSocket, buffer->data(), buffer->capacity(), 0, sizeof(MsgBind), 5000); // 5 seconds timeout
  if (result != sizeof(MsgBind)) {
    Log::getInstance().error(std::format("Failed to recv tcp data with return code: {}", result));
    SocketClose(tcpSocket);
    return false;
  }

  // Resize buffer to actual data size
  buffer->resize(sizeof(MsgBind));

  MsgBind bind;
  UvtUtils::ExtractMsgBind(*buffer, bind);

  auto & allowedClientIds = Configuration::getInstance()->getAllowedClientIds();

  // Log the list of allowed client IDs for debugging
  std::string allowedIdsStr = "Allowed client IDs: ";
  for (auto id : allowedClientIds) {
    allowedIdsStr += std::to_string(id) + ", ";
  }
  Log::getInstance().info(allowedIdsStr);
  
      clientId = bind.clientId;
      auto bAllow = allowedClientIds.find(clientId) != allowedClientIds.end();
  if(bAllow) {
    Log::getInstance().info(std::format("Client Id: {} is allowed", bind.clientId));
    
    // Create a new connection for this client
    pConnection connection = ConnectionManager::getInstance().createConnection(bind.clientId);
    MsgBindResponse bindResponse;
    bindResponse.connectionId = connection->connectionId;
    
    // Get a buffer for the acceptance response
    auto acceptBuffer = MemoryPool::getInstance().getBuffer(0);
    UvtUtils::AppendMsgBindResponse(bindResponse, *acceptBuffer);
    
    // Send acceptance with timeout
    Log::getInstance().info(std::format("Sending acceptance response to client {}, connectionId: {}", bind.clientId, bindResponse.connectionId));
    int result = SendTcpDataNonBlocking(tcpSocket, acceptBuffer->data(), acceptBuffer->size(), 0, 5000);
    
    // Check result of sending
    if (result <= 0) {
      Log::getInstance().error(std::format("Failed to send acceptance response with error code: {}", result));
      MemoryPool::getInstance().recycleBuffer(acceptBuffer);
      SocketClose(tcpSocket);
      return false;
    }
    
    // Recycle buffer
    MemoryPool::getInstance().recycleBuffer(acceptBuffer);
  } else {
    Log::getInstance().error(std::format("Client Id: {} is not allowed", bind.clientId));
    
    // Send a rejection response instead of silently closing the socket
    MsgBindResponse rejectResponse;
    rejectResponse.connectionId = 0; // Use 0 to indicate rejection
    
    // Get a buffer for the reject response
    auto rejectBuffer = MemoryPool::getInstance().getBuffer(0);
    UvtUtils::AppendMsgBindResponse(rejectResponse, *rejectBuffer);
    
    // Send rejection with timeout
    Log::getInstance().info("Sending rejection response to unauthorized client");
    SendTcpDataNonBlocking(tcpSocket, rejectBuffer->data(), rejectBuffer->size(), 0, 5000);
    
    // Recycle buffer and close socket
    MemoryPool::getInstance().recycleBuffer(rejectBuffer);
    SocketClose(tcpSocket);
    return false;
  }

  // Connection was already created and bind response already sent
  // Use the connectionId already created and sent in the bind response

  // Get or create a UDP socket for this client
  udpSocket = ClientUdpSocketManager::getInstance().getOrCreateUdpSocket(clientId);
  if (udpSocket < 0) {
    Log::getInstance().error(std::format("Failed to create UDP socket for client ID: {}", bind.clientId));
    SocketClose(tcpSocket);
    return false;
  }

  Log::getInstance().info(std::format("Created UDP socket for client ID: {}, Socket: {}", bind.clientId, udpSocket));

  return true;
}

