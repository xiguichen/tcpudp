#pragma once

#include "BlockingQueue.h"
#include <Socket.h>
#include <atomic>
#include <thread>
#include <vector>

class SocketManager {
public:
  SocketManager();
  ~SocketManager();

  void createSocket();
  void bindToPort(int port);
  void listenForConnections();
  void acceptConnection();
  void startTcpToQueueThread(SocketFd clientSocket, SocketFd udpSocket,
                             uint32_t clientId);
  void startUdpToQueueThread(int clientSocket, BlockingQueue &queue);
  void startTcpQueueToUdpThread(const SocketFd& udpSocket, BlockingQueue &queue);
  void startUdpQueueToTcpThread(int clientSocket, BlockingQueue &queue) {}
  bool CapabilityNegotiate(SocketFd socket, uint32_t &clientId,
                           SocketFd &udpSocket);

  // Shutdown mechanism
  void shutdown();
  bool isRunning() const;

  // Static method to check if server is running
  static bool isServerRunning();

private:
  int serverSocket;
  std::vector<std::thread> threads;

  // Static atomic flag for controlling all threads
  static std::atomic<bool> s_running;

  // Prevent threads from joining multiple times during cleanup
  bool threadsJoined;
};
