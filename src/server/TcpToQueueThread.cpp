#include "TcpToQueueThread.h"
#include "TcpDataQueue.h"
#include "SocketManager.h"
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
#include "ClientUdpSocketManager.h"
#include "TcpToUdpSocketMap.h"
#include "UdpToTcpSocketMap.h"
#include "UdpToQueueThread.h"

using namespace Logger;

void TcpToQueueThread::run() {
  // Initialize memory monitoring
  MemoryMonitor::getInstance().start();
  
  // Get a buffer from the memory pool with optimal size for TCP packets
  const int bufferSize = 65535;
  auto buffer = MemoryPool::getInstance().getBuffer(bufferSize);
  
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
  int result = RecvTcpDataWithSizeNonBlocking(socket_, buffer->data(), buffer->capacity(), 0, sizeof(MsgBind), 5000); // 5 seconds timeout
  if (result != sizeof(MsgBind)) {
    Log::getInstance().error(std::format("Failed to recv tcp data with return code: {}", result));
    SocketClose(socket_);
    return;
  }

  // Resize buffer to actual data size
  buffer->resize(sizeof(MsgBind));

  MsgBind bind;
  UvtUtils::ExtractMsgBind(*buffer, bind);

  auto & allowedClientIds = Configuration::getInstance()->getAllowedClientIds();

  auto bAllow = allowedClientIds.find(bind.clientId) != allowedClientIds.end();
  if(bAllow) {
    Log::getInstance().info(std::format("Client Id: {} is allowed", bind.clientId));
  } else {
    Log::getInstance().error(std::format("Client Id: {} is not allowed", bind.clientId));
    SocketClose(socket_);
    return;
  }

  // Create a new connection for this client
  pConnection connection = ConnectionManager::getInstance().createConnection(bind.clientId);
  MsgBindResponse bindResponse;
  bindResponse.connectionId = connection->connectionId;

  // Get or create a UDP socket for this client
  int udpSocket = ClientUdpSocketManager::getInstance().getOrCreateUdpSocket(bind.clientId);
  if (udpSocket < 0) {
    Log::getInstance().error(std::format("Failed to create UDP socket for client ID: {}", bind.clientId));
    SocketClose(socket_);
    return;
  }

  // Map the TCP and UDP sockets
  UdpToTcpSocketMap::getInstance().mapSockets(udpSocket, socket_);
  TcpToUdpSocketMap::getInstance().mapSockets(socket_, udpSocket);
  
  // Start UDP thread if this is the first connection for this client
  if (ConnectionManager::getInstance().getClientConnectionCount(bind.clientId) == 1) {
    // Start a thread to handle UDP data for this client
    startUdpToQueueThread(udpSocket);
    Log::getInstance().info(std::format("Started UDP thread for client ID: {}", bind.clientId));
  }
  
  Log::getInstance().info(std::format("Mapped TCP socket {} to UDP socket {} for client ID: {}", 
                                     socket_, udpSocket, bind.clientId));

  // Get a buffer for the response
  auto outputBuffer = MemoryPool::getInstance().getBuffer(0);
  UvtUtils::AppendMsgBindResponse(bindResponse, *outputBuffer);

  // Send response with timeout
  result = SendTcpDataNonBlocking(socket_, outputBuffer->data(), outputBuffer->size(), 0, 5000); // 5 seconds timeout
  if (result != static_cast<int>(outputBuffer->size())) {
    Log::getInstance().error(std::format("Failed to send tcp data with return code: {}", result));
    SocketClose(socket_);
    return;
  }

  // Recycle the output buffer
  MemoryPool::getInstance().recycleBuffer(outputBuffer);

  Log::getInstance().info("End capability negotiate");

  // Get buffers for data processing from the memory pool
  auto remainingData = MemoryPool::getInstance().getBuffer(0);
  auto decodedData = MemoryPool::getInstance().getBuffer(0);
  
  // Reset the receive buffer for reuse
  buffer->clear();
  buffer->resize(bufferSize);

  // Main processing loop
  while (SocketManager::isServerRunning()) {
    // Check if socket is readable with a short timeout
    if (IsSocketReadable(socket_, 100)) { // 100ms timeout
      Log::getInstance().info("Client -> Server (Receive TCP Data)");
      
      // Receive data non-blocking with timeout
      result = RecvTcpDataNonBlocking(socket_, buffer->data(), buffer->capacity(), 0, 1000); // 1 second timeout
      
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
      
      // Track memory allocation
      MemoryMonitor::getInstance().trackAllocation(result);
      
      // Resize buffer to actual data size
      buffer->resize(result);
      
      // Process received data
      remainingData->insert(remainingData->end(), buffer->begin(), buffer->end());
      
      // Reset buffer for next receive
      buffer->clear();
      buffer->resize(bufferSize);

      do {
        // Extract UDP data from the TCP stream
        *remainingData = UvtUtils::ExtractUdpData(*remainingData, *decodedData);
        
        if (decodedData->size()) {
          // Create a new buffer for the queue
          auto queueBuffer = MemoryPool::getInstance().getBuffer(decodedData->size());
          queueBuffer->assign(decodedData->begin(), decodedData->end());
          
          // Enqueue the data
          enqueueData(queueBuffer);
          
          // Clear decoded data for next extraction
          decodedData->clear();
        }
      } while (decodedData->size() && remainingData->size());
    } else {
      // No data available, yield to other threads
      std::this_thread::yield();
    }
    
    // Periodically log memory usage (every ~1000 iterations)
    static int counter = 0;
    if (++counter % 1000 == 0) {
      MemoryMonitor::getInstance().logMemoryUsage();
      
      // Trim the memory pool to prevent memory bloat
      MemoryPool::getInstance().trim(0.7f);
    }
  }
  
  // Recycle all buffers
  MemoryPool::getInstance().recycleBuffer(buffer);
  MemoryPool::getInstance().recycleBuffer(remainingData);
  MemoryPool::getInstance().recycleBuffer(decodedData);
}

void TcpToQueueThread::enqueueData(const char *data, size_t length) {
  // Create a buffer from the memory pool
  auto dataVector = MemoryPool::getInstance().getBuffer(length);
  
  // Copy the data
  dataVector->assign(data, data + length);
  
  // Enqueue the data
  TcpDataQueue::getInstance().enqueue(socket_, dataVector);
  
  Log::getInstance().info(
      std::format("TCP -> Queue: Decoded Data enqueued. Length: {}", length));
}

void TcpToQueueThread::enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer) {
  // The buffer is already properly sized by the caller
  Log::getInstance().info(
      std::format("TCP -> Queue: Decoded Data enqueued. Length: {}", dataBuffer->size()));
  
  // Enqueue the data buffer directly (no need to copy)
  TcpDataQueue::getInstance().enqueue(socket_, dataBuffer);
  
  // Note: The buffer is now owned by the queue and will be recycled when no longer needed
}

void TcpToQueueThread::startUdpToQueueThread(int udpSocket) {
  auto thread = std::thread([udpSocket]() {
    UdpToQueueThread udpToQueueThread(udpSocket);
    udpToQueueThread.run();
  });
  thread.detach(); // Detach the thread to let it run independently
}
