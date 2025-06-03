#include "ServerQueueManager.h"

std::shared_ptr<BlockingQueue> ServerQueueManager::getQueueForClient(int clientId, std::unordered_map<int, std::shared_ptr<BlockingQueue>>& queueMap) {
    std::lock_guard<std::mutex> lock(mutex_); // Lock the mutex for thread safety

    // Check if the queue for the client ID already exists
    auto it = queueMap.find(clientId);
    if (it != queueMap.end()) {
        return it->second; // Return the existing queue
    }

    // Create a new BlockingQueue for the client ID
    auto newQueue = std::make_shared<BlockingQueue>();
    queueMap[clientId] = newQueue;
    return newQueue;
}

std::shared_ptr<BlockingQueue> ServerQueueManager::getTcpToUdpQueueForClient(int clientId) {
    return getQueueForClient(clientId, tcpToUdpQueues_);
}

std::shared_ptr<BlockingQueue> ServerQueueManager::getUdpToTcpQueueForClient(int clientId) {
    return getQueueForClient(clientId, udpToTcpQueues_);
}
