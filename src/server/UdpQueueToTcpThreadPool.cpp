#include "UdpQueueToTcpThreadPool.h"

#include "SocketManager.h"
#include <Log.h>
#include <Socket.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <Protocol.h>
#include <format>
#include <thread>
#include <MemoryPool.h>
#include <atomic>

using namespace Logger;

static std::atomic<uint8_t> gMsgId(0);


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
    auto data = udpDataQueue->dequeue();
    Log::getInstance().info("Get data from UDP queue...");

    // use gMsgId as the msg id and increase it
    uint8_t msgId = gMsgId.fetch_add(1, std::memory_order_relaxed);
    auto newData = MemoryPool::getInstance().getBuffer(data->size() + 20);
    UvtUtils::AppendUdpData(*data, msgId, *newData);

    // Send data via TCP
    Log::getInstance().info(
        std::format("Server -> Client (TCP Data), Length: {}", data->size()));
    sendDataViaTcp(tcpSocket_, *data);
  }
}

void UdpQueueToTcpThreadPool::sendDataViaTcp(int tcpSocket,
                                             const std::vector<char> &data) {
  if (tcpSocket != -1) {
    SendTcpData(tcpSocket, data.data(), data.size(), 0);
  } else {
    Log::getInstance().error("Queue -> TCP: Invalid socket");
  }
}
