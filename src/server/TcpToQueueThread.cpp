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
#include <format>
#include <iostream>
#include <vector>
#include <cstring>

using namespace Logger;

void TcpToQueueThread::run() {
  char buffer[65535];
  int result = SocketSelect(socket_, 2);
  if(result <= 0) {
    Log::getInstance().error("Socket select failed");
    return;
  }
  result = RecvTcpData(socket_, buffer, 5, 0);
  if(memcpy(buffer, "0.0.1", 5) != 0) {
    Log::getInstance().error("Invalid protocol version");
    SocketClose(socket_);
    return;
  }

  while (true) {
    ssize_t bytesRead = readFromSocket(buffer, sizeof(buffer));
    if (bytesRead > 0) {
      enqueueData(buffer, bytesRead);
    } else {
      break;
    }
  }
}

size_t TcpToQueueThread::readFromSocket(char *buffer, size_t bufferSize) {

  DataHeader header;

  size_t lengthBytesRead = RecvTcpData(socket_, &header, HEADER_SIZE, 0);
  if (lengthBytesRead != HEADER_SIZE) {
    if (lengthBytesRead == 0) {
      Log::getInstance().error("Connection closed by peer. Length=0");
      SocketClose(socket_);
    } else {
      Log::getInstance().error("Connection closed by peer. Length not correct");
    }
    return 0;
  }

  // Convert message length from network byte order to host byte order
  uint16_t messageLength = ntohs(header.size);

  Log::getInstance().info(std::format(
      "TCP -> Queue: Data ID {} , Data Length: {}, Data Checksum: {}",
      header.id, messageLength, header.checksum));

  // Ensure the buffer is large enough
  if (messageLength > bufferSize) {
    std::cerr << "Buffer size is too small for the incoming message"
              << std::endl;
    Log::getInstance().error(
        std::format("TCP -> Queue: Buffer size less than {}", messageLength));
    exit(1);
    return 0;
  }

  uint16_t total_received = 0;
  while (total_received < messageLength) {
    int bytes = RecvTcpData(socket_, buffer + total_received,
                            messageLength - total_received, 0);
    if (bytes <= 0)
      break;
    total_received += bytes;
  }
  uint8_t checksum = xor_checksum((uint8_t *)buffer, messageLength);
  if (checksum != header.checksum) {
    Log::getInstance().error("Tcp -> Queue: Checksum verify failed");
    return {};
  }

  return total_received;
}

void TcpToQueueThread::enqueueData(const char *data, size_t length) {
  auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
  TcpDataQueue::getInstance().enqueue(socket_, dataVector);
  Log::getInstance().info("TCP -> Queue: Data enqueued.");
}
