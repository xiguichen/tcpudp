#include "UdpQueueToTcpThreadPool.h"

#include "UdpDataQueue.h"
#include "UdpToTcpSocketMap.h"
#include "SocketManager.h"
#include <Log.h>
#include <Socket.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <Protocol.h>
#include <cstring>
#include <format>
#include <thread>

using namespace Logger;

void UdpQueueToTcpThreadPool::run() {
  // Start a thread to process data concurrently
  std::thread processingThread(
      &UdpQueueToTcpThreadPool::processDataConcurrently, this);
  processingThread.detach(); // Detach the thread to run independently
}

void UdpQueueToTcpThreadPool::processDataConcurrently() {
  while (SocketManager::isServerRunning()) {
    // Dequeue data from the UDP data queue
    Log::getInstance().info("Processing data from UDP queue...");
    auto dataPair = UdpDataQueue::getInstance().dequeue();
    Log::getInstance().info("Get data from UDP queue...");
    if (dataPair.first != -1) {
      // Extract socket and data
      int udpSocket = dataPair.first;
      auto data = dataPair.second;

      // Retrieve mapped TCP socket
      int tcpSocket =
          UdpToTcpSocketMap::getInstance().retrieveMappedTcpSocket(udpSocket);

      // Send data via TCP
      Log::getInstance().info(std::format("Server -> Client (TCP Data), Length: {}", data->size()));
      sendDataViaTcp(tcpSocket, *data);
    }
  }
}

void UdpQueueToTcpThreadPool::sendDataViaTcp(
    int tcpSocket, const std::vector<char> &data) {
  if (tcpSocket != -1) {
    SendTcpData(tcpSocket, data.data(), data.size(), 0);
  } else {
    Log::getInstance().error("Queue -> TCP: Invalid socket");
  }
}
