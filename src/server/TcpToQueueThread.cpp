#include "TcpToQueueThread.h"
#include "TcpDataQueue.h"
#include <Log.h>
#include <Protocol.h>
#include <Socket.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#endif
#include <cstring>
#include <format>
#include <vector>

using namespace Logger;

void TcpToQueueThread::run() {
  const int bufferSize = 65535;
  char buffer[bufferSize];
  int result = SocketSelect(socket_, 2);
  if (result <= 0) {
    Log::getInstance().error("Socket select failed");
    SocketClose(socket_);
    return;
  }

  Log::getInstance().info("Begin capability negotiate");

  result = RecvTcpData(socket_, buffer, 5, 0);
  if (memcmp(buffer, "0.0.1", 5) != 0) {
    Log::getInstance().error("Invalid protocol version");
    SocketClose(socket_);
    return;
  }

  Log::getInstance().info("End capability negotiate");

  std::vector<char> remainingData;
  std::vector<char> decodedData;

  while (true) {

    Log::getInstance().info("Client -> Server (Receive TCP Data)");
    result = RecvTcpData(socket_, buffer, bufferSize, 0);
    if (result == 0 || result == -1) {
      Log::getInstance().error(
          std::format("Failed to recv tcp data with return code: {}", result));
      break;
    }

    remainingData.insert(remainingData.end(), buffer, buffer + result);

    do {
      remainingData = UvtUtils::ExtractUdpData(remainingData, decodedData);
      if (decodedData.size()) {
        enqueueData(decodedData.data(), decodedData.size());
      }
    } while (decodedData.size() && remainingData.size());
  }
}

void TcpToQueueThread::enqueueData(const char *data, size_t length) {
  auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
  TcpDataQueue::getInstance().enqueue(socket_, dataVector);
  Log::getInstance().info(std::format("TCP -> Queue: Decoded Data enqueued. Length: {}", length));
}
