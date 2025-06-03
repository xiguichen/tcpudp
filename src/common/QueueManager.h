#pragma once

#include "BlockingQueue.h" // Include the BlockingQueue header
#include <memory>
#include <mutex>
#include <unordered_map>

class QueueManager {
public:
  // Get the singleton instance
  static QueueManager &getInstance() {
    static QueueManager instance;
    return instance;
  }

  // Get or create a BlockingQueue for a given client ID
  std::shared_ptr<BlockingQueue> getTcpToUdpQueueForClient(int clientId);

  // Get or create a BlockingQueue for a given client ID
  std::shared_ptr<BlockingQueue> getUdpToTcpQueueForClient(int clientId);

  // Delete copy constructor and assignment operator
  QueueManager(const QueueManager &) = delete;
  QueueManager &operator=(const QueueManager &) = delete;


private:

  std::shared_ptr<BlockingQueue> getQueueForClient(int clientId, std::unordered_map<int, std::shared_ptr<BlockingQueue>>& queueMap); 

  // Private constructor
  QueueManager() = default;

  std::unordered_map<int, std::shared_ptr<BlockingQueue>> tcpToUdpQueues_;

  std::unordered_map<int, std::shared_ptr<BlockingQueue>> udpToTcpQueues_;

  std::mutex mutex_; // Mutex to protect access to the map
};

