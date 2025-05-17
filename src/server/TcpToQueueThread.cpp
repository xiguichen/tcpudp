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
#include "Configuration.h"
#include "Protocol.h"

using namespace Logger;

void TcpToQueueThread::run() {
  const int bufferSize = 65535;
  char buffer[bufferSize];
  
  // Set socket to non-blocking mode
  SetSocketNonBlocking(socket_);
  
  // Wait for socket to be readable with timeout
  if (!IsSocketReadable(socket_, 2000)) { // 2 seconds timeout
    Log::getInstance().error("Socket not readable within timeout");
    SocketClose(socket_);
    return;
  }

  Log::getInstance().info("Begin capability negotiate");

  // Receive handshake data with timeout
  int result = RecvTcpDataWithSizeNonBlocking(socket_, buffer, bufferSize, 0, sizeof(MsgBind), 5000); // 5 seconds timeout
  if (result != sizeof(MsgBind)) {
    Log::getInstance().error(std::format("Failed to recv tcp data with return code: {}", result));
    SocketClose(socket_);
    return;
  }

  MsgBind bind;
  std::vector<char> bindData(buffer, buffer + sizeof(MsgBind));
  UvtUtils::ExtractMsgBind(bindData, bind);

  auto & allowedClientIds = Configuration::getInstance()->getAllowedClientIds();

  auto bAllow = allowedClientIds.find(bind.clientId) != allowedClientIds.end();
  if(bAllow) {
    Log::getInstance().info(std::format("Client Id: {} is allowed", bind.clientId));
  } else {
    Log::getInstance().error(std::format("Client Id: {} is not allowed", bind.clientId));
    SocketClose(socket_);
    return;
  }

  pConnection connection = ConnectionManager::getInstance().createConnection(bind.clientId);
  MsgBindResponse bindResponse;
  bindResponse.connectionId = connection->connectionId;

  std::vector<char> outputBuffer;
  UvtUtils::AppendMsgBindResponse(bindResponse, outputBuffer);

  // Send response with timeout
  result = SendTcpDataNonBlocking(socket_, outputBuffer.data(), outputBuffer.size(), 0, 5000); // 5 seconds timeout
  if (result != static_cast<int>(outputBuffer.size())) {
    Log::getInstance().error(std::format("Failed to send tcp data with return code: {}", result));
    SocketClose(socket_);
    return;
  }

  Log::getInstance().info("End capability negotiate");

  std::vector<char> remainingData;
  std::vector<char> decodedData;

  // Main processing loop
  while (true) {
    // Check if socket is readable with a short timeout
    if (IsSocketReadable(socket_, 100)) { // 100ms timeout
      Log::getInstance().info("Client -> Server (Receive TCP Data)");
      
      // Receive data non-blocking with timeout
      result = RecvTcpDataNonBlocking(socket_, buffer, bufferSize, 0, 1000); // 1 second timeout
      
      if (result == SOCKET_ERROR_TIMEOUT) {
        // Timeout, just continue the loop
        continue;
      } else if (result == SOCKET_ERROR_WOULD_BLOCK) {
        // Would block, just continue the loop
        continue;
      } else if (result == SOCKET_ERROR_CLOSED || result <= 0) {
        // Connection closed or error
        Log::getInstance().error(
            std::format("Failed to recv tcp data with return code: {}", result));
        break;
      }
      
      // Process received data
      remainingData.insert(remainingData.end(), buffer, buffer + result);

      do {
        remainingData = UvtUtils::ExtractUdpData(remainingData, decodedData);
        if (decodedData.size()) {
          enqueueData(decodedData.data(), decodedData.size());
        }
      } while (decodedData.size() && remainingData.size());
    } else {
      // No data available, yield to other threads
      std::this_thread::yield();
    }
  }
}

void TcpToQueueThread::enqueueData(const char *data, size_t length) {
  auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
  TcpDataQueue::getInstance().enqueue(socket_, dataVector);
  Log::getInstance().info(
      std::format("TCP -> Queue: Decoded Data enqueued. Length: {}", length));
}
