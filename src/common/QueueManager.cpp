#include "QueueManager.h"

BlockingQueue* QueueManager::getQueueForClient(int clientId, std::unordered_map<int, std::shared_ptr<BlockingQueue>>& queueMap) {
    std::lock_guard<std::mutex> lock(mutex_); // Lock the mutex for thread safety

    // Check if the queue for the client ID already exists
    auto it = queueMap.find(clientId);
    if (it != queueMap.end()) {
        return it->second.get(); // Return the existing queue
    }

    // Create a new BlockingQueue for the client ID
    auto newQueue = std::make_shared<BlockingQueue>();
    queueMap[clientId] = newQueue;
    return newQueue.get();
}

BlockingQueue* QueueManager::getTcpToUdpQueueForClient(int clientId) {
    return getQueueForClient(clientId, tcpToUdpQueues_);
}

BlockingQueue* QueueManager::getUdpToTcpQueueForClient(int clientId) {
    return getQueueForClient(clientId, udpToTcpQueues_);
}
