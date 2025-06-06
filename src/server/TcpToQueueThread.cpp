#include "TcpToQueueThread.h"
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
#include "Protocol.h"
#include "UdpToQueueThread.h"
#include "QueueManager.h"

using namespace Logger;

void TcpToQueueThread::run() {
  // Initialize memory monitoring
  MemoryMonitor::getInstance().start();
  
  const int bufferSize = 65535;
  auto buffer = MemoryPool::getInstance().getBuffer(bufferSize);

  // Log that the handshake response was already sent earlier
  Log::getInstance().info("Handshake response already sent during client authorization check");

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
      auto result = RecvTcpDataNonBlocking(socket_, buffer->data(), buffer->capacity(), 0, 1000); // 1 second timeout
      
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

void TcpToQueueThread::enqueueData(std::shared_ptr<std::vector<char>>& dataBuffer) {

  // The buffer is already properly sized by the caller
  Log::getInstance().info(
      std::format("TCP -> Queue: Decoded Data enqueued. Length: {}", dataBuffer->size()));
  
  // Enqueue the data buffer directly (no need to copy)
  QueueManager::getInstance().getTcpToUdpQueueForClient(clientId_)->enqueue(dataBuffer);
  
  // Note: The buffer is now owned by the queue and will be recycled when no longer needed
}
