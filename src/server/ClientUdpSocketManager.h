#ifndef CLIENT_UDP_SOCKET_MANAGER_H
#define CLIENT_UDP_SOCKET_MANAGER_H

#include <map>
#include <mutex>
#include <Socket.h>

class ClientUdpSocketManager {
public:
    // Get the singleton instance
    static ClientUdpSocketManager& getInstance() {
        static ClientUdpSocketManager instance;
        return instance;
    }

    // Delete copy constructor and assignment operator
    ClientUdpSocketManager(const ClientUdpSocketManager&) = delete;
    ClientUdpSocketManager& operator=(const ClientUdpSocketManager&) = delete;

    // Get or create a UDP socket for a client
    int getOrCreateUdpSocket(uint32_t clientId) {
        std::lock_guard<std::mutex> lock(mutex);
        
        // Check if client already has a UDP socket
        auto it = clientUdpSockets.find(clientId);
        if (it != clientUdpSockets.end()) {
            return it->second;
        }
        
        // Create a new UDP socket for this client
        int udpSocket = CreateSocket(AF_INET, SOCK_DGRAM, 0);
        if (udpSocket < 0) {
            return -1;
        }
        
        // Set UDP socket to non-blocking mode
        if (SetSocketNonBlocking(udpSocket) < 0) {
            SocketClose(udpSocket);
            return -1;
        }
        
        // Store the UDP socket for this client
        clientUdpSockets[clientId] = udpSocket;
        return udpSocket;
    }
    
    // Get the UDP socket for a client
    int getUdpSocket(uint32_t clientId) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = clientUdpSockets.find(clientId);
        if (it != clientUdpSockets.end()) {
            return it->second;
        }
        
        return -1; // No UDP socket found for this client
    }
    
    // Remove a UDP socket for a client
    void removeUdpSocket(uint32_t clientId) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = clientUdpSockets.find(clientId);
        if (it != clientUdpSockets.end()) {
            SocketClose(it->second);
            clientUdpSockets.erase(it);
        }
    }
    
    // Clean up all UDP sockets
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex);
        
        for (auto& pair : clientUdpSockets) {
            SocketClose(pair.second);
        }
        
        clientUdpSockets.clear();
    }

private:
    // Private constructor
    ClientUdpSocketManager() = default;
    
    // Destructor
    ~ClientUdpSocketManager() {
        cleanup();
    }

private:
    // Map of client IDs to their UDP sockets
    std::map<uint32_t, int> clientUdpSockets;
    
    // Mutex for thread-safe operations
    std::mutex mutex;
};

#endif // CLIENT_UDP_SOCKET_MANAGER_H