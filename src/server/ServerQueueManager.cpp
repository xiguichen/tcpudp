#include "ServerQueueManager.h"

std::shared_ptr<BlockingQueue> ServerQueueManager::getQueueForClient(int clientId) {
    std::lock_guard<std::mutex> lock(mutex_); // Lock the mutex for thread safety

    // Check if the queue for the client ID already exists
    auto it = clientQueues_.find(clientId);
    if (it != clientQueues_.end()) {
        return it->second; // Return the existing queue
    }

    // Create a new BlockingQueue for the client ID
    auto newQueue = std::make_shared<BlockingQueue>();
    clientQueues_[clientId] = newQueue;
    return newQueue;
}
