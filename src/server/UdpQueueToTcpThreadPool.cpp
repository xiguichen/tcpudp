#include "UdpQueueToTcpThreadPool.h"

#include "UdpDataQueue.h"
#include "UdpToTcpSocketMap.h"
#include <Log.h>
#include <Socket.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <thread>
#include <Protocol.h>
#include <format>
#include <cstring>

using namespace Logger;

void UdpQueueToTcpThreadPool::run() {
  // Start a thread to process data concurrently
  std::thread processingThread(
      &UdpQueueToTcpThreadPool::processDataConcurrently, this);
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
      int tcpSocket =
          UdpToTcpSocketMap::getInstance().retrieveMappedTcpSocket(udpSocket);

      // Send data via TCP
      sendDataViaTcp(tcpSocket, data);
    }
  }
}

void UdpQueueToTcpThreadPool::sendDataViaTcp(
    int tcpSocket, const std::shared_ptr<std::vector<char>> &data) {
    if (tcpSocket != -1) {

        std::vector<char> newData;
        UvtUtils::AppendUdpData(*data, sendId, newData);
        SendTcpData(tcpSocket, newData.data(), newData.size(), 0);

    } else {
        Log::getInstance().error("Queue -> TCP: Invalid socket");
    }
}
