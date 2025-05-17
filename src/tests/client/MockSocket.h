#ifndef MOCK_SOCKET_H
#define MOCK_SOCKET_H

#include "ISocket.h"
#include <vector>
#include <string>
#include <stdexcept>

// Mock implementation of ISocket for testing
class MockSocket : public ISocket {
public:
    MockSocket() = default;
    
    // ISocket interface implementation
    void connect(const std::string& address, int port) override {
        if (!shouldConnect) {
            throw std::runtime_error("Connection failed (mock)");
        }
        
        connectedAddress = address;
        connectedPort = port;
        isConnected = true;
    }
    
    void send(const std::vector<char>& data) override {
        if (!isConnected) {
            throw std::runtime_error("Not connected (mock)");
        }
        
        sentData.insert(sentData.end(), data.begin(), data.end());
    }
    
    std::vector<char> receive() override {
        if (!isConnected) {
            throw std::runtime_error("Not connected (mock)");
        }
        
        if (receiveQueue.empty()) {
            return {};
        }
        
        std::vector<char> data = receiveQueue.front();
        receiveQueue.erase(receiveQueue.begin());
        return data;
    }
    
    void close() override {
        isConnected = false;
    }
    
    // Mock-specific methods
    void queueDataForReceive(const std::vector<char>& data) {
        receiveQueue.push_back(data);
    }
    
    void setShouldConnect(bool value) {
        shouldConnect = value;
    }
    
    bool isSocketConnected() const {
        return isConnected;
    }
    
    const std::vector<char>& getSentData() const {
        return sentData;
    }
    
    std::string getConnectedAddress() const {
        return connectedAddress;
    }
    
    int getConnectedPort() const {
        return connectedPort;
    }
    
private:
    bool isConnected = false;
    bool shouldConnect = true;
    std::string connectedAddress;
    int connectedPort = 0;
    std::vector<char> sentData;
    std::vector<std::vector<char>> receiveQueue;
};

#endif // MOCK_SOCKET_H