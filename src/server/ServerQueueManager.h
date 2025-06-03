#ifndef SERVER_QUEUE_MANAGER_H
#define SERVER_QUEUE_MANAGER_H

#include "BlockingQueue.h" // Include the BlockingQueue header
#include <memory>
#include <mutex>
#include <unordered_map>

class ServerQueueManager {
public:
  // Get the singleton instance
  static ServerQueueManager &getInstance() {
    static ServerQueueManager instance;
    return instance;
  }

  // Get or create a BlockingQueue for a given client ID
  std::shared_ptr<BlockingQueue> getQueueForClient(int clientId);

  // Delete copy constructor and assignment operator
  ServerQueueManager(const ServerQueueManager &) = delete;
  ServerQueueManager &operator=(const ServerQueueManager &) = delete;

private:
  // Private constructor
  ServerQueueManager() = default;

  std::unordered_map<int, std::shared_ptr<BlockingQueue>> clientQueues_;
  std::mutex mutex_; // Mutex to protect access to the map
};

#endif // SERVER_QUEUE_MANAGER_H
