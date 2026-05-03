# Usage Examples

This document provides practical examples for using the TCP/UDP project in various scenarios.

## Table of Contents

1. [Basic Examples](#basic-examples)
2. [Advanced Examples](#advanced-examples)
3. [Performance Examples](#performance-examples)
4. [Integration Examples](#integration-examples)
5. [Real-world Scenarios](#real-world-scenarios)

## Basic Examples

### Example 1: Simple Echo Server

```cpp
// src/examples/echo_server.cpp
#include <iostream>
#include <thread>
#include <chrono>
#include "Server.h"
#include "Log.h"

class EchoHandler {
public:
    void onDataReceived(int clientId, const void* data, size_t length) {
        std::string message(reinterpret_cast<const char*>(data), length);
        log_info("Received from client %d: %s", clientId, message.c_str());
        
        // Echo back to client
        server->sendToClient(clientId, data, length);
    }
    
    void onClientConnected(int clientId) {
        log_info("Client %d connected", clientId);
    }
    
    void onClientDisconnected(int clientId) {
        log_info("Client %d disconnected", clientId);
    }
    
    void setServer(Server* srv) {
        server = srv;
    }
    
private:
    Server* server;
};

int main() {
    Server server;
    
    EchoHandler handler;
    handler.setServer(&server);
    
    // Set up callbacks
    server.setDataHandler([&handler](int clientId, const void* data, size_t length) {
        handler.onDataReceived(clientId, data, length);
    });
    
    server.setConnectionHandler([&handler](int clientId, bool connected) {
        if (connected) {
            handler.onClientConnected(clientId);
        } else {
            handler.onClientDisconnected(clientId);
        }
    });
    
    // Start server
    if (!server.start()) {
        log_error("Failed to start server");
        return 1;
    }
    
    log_info("Echo server started on port 7001");
    
    // Run until interrupted
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

### Example 2: Basic Client

```cpp
// src/examples/basic_client.cpp
#include <iostream>
#include <thread>
#include <chrono>
#include "Client.h"
#include "Log.h"

int main() {
    Client client;
    
    // Set up callbacks
    client.setDataHandler([](const void* data, size_t length) {
        std::string message(reinterpret_cast<const char*>(data), length);
        log_info("Received from server: %s", message.c_str());
    });
    
    client.setConnectionHandler([](bool connected) {
        if (connected) {
            log_info("Connected to server");
        } else {
            log_info("Disconnected from server");
        }
    });
    
    // Connect to server
    std::string serverAddress = "localhost";
    int serverPort = 7001;
    
    log_info("Connecting to %s:%d", serverAddress.c_str(), serverPort);
    
    if (!client.connect(serverAddress, serverPort)) {
        log_error("Failed to connect to server");
        return 1;
    }
    
    // Send messages
    for (int i = 0; i < 10; i++) {
        std::string message = "Hello " + std::to_string(i);
        if (!client.sendUDPData(message.c_str(), message.length())) {
            log_error("Failed to send message %d", i);
            break;
        }
        log_info("Sent: %s", message.c_str());
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Keep connection alive for a bit
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    return 0;
}
```

### Example 3: Command Line Tool

```cpp
// src/examples/cli_tool.cpp
#include <iostream>
#include <string>
#include "Client.h"
#include "Server.h"
#include "Log.h"

class CommandLineInterface {
public:
    void run() {
        std::string input;
        bool running = true;
        
        while (running) {
            std::cout << "> ";
            std::getline(std::cin, input);
            
            if (input.empty()) continue;
            
            // Parse command
            if (input.substr(0, 4) == "send") {
                handleSendCommand(input.substr(5));
            } else if (input == "connect") {
                handleConnectCommand();
            } else if (input == "disconnect") {
                handleDisconnectCommand();
            } else if (input == "status") {
                handleStatusCommand();
            } else if (input == "help") {
                handleHelpCommand();
            } else if (input == "exit") {
                running = false;
            } else {
                std::cout << "Unknown command. Type 'help' for usage.\n";
            }
        }
    }
    
private:
    Client client;
    
    void handleSendCommand(const std::string& message) {
        if (client.isConnected()) {
            if (client.sendUDPData(message.c_str(), message.length())) {
                std::cout << "Message sent: " << message << "\n";
            } else {
                std::cout << "Failed to send message\n";
            }
        } else {
            std::cout << "Not connected. Use 'connect' first.\n";
        }
    }
    
    void handleConnectCommand() {
        std::string address;
        int port;
        
        std::cout << "Server address: ";
        std::cin >> address;
        
        std::cout << "Server port: ";
        std::cin >> port;
        
        if (client.connect(address, port)) {
            std::cout << "Connected to " << address << ":" << port << "\n";
        } else {
            std::cout << "Connection failed\n";
        }
        
        // Clear input buffer
        std::cin.ignore();
    }
    
    void handleDisconnectCommand() {
        client.disconnect();
        std::cout << "Disconnected\n";
    }
    
    void handleStatusCommand() {
        if (client.isConnected()) {
            std::cout << "Connected to server\n";
            std::cout << "Client ID: " << client.getClientId() << "\n";
        } else {
            std::cout << "Not connected\n";
        }
    }
    
    void handleHelpCommand() {
        std::cout << "Available commands:\n";
        std::cout << "  connect      - Connect to server\n";
        std::cout << "  disconnect   - Disconnect from server\n";
        std::cout << "  send <msg>   - Send message to server\n";
        std::cout << "  status       - Show connection status\n";
        std::cout << "  help         - Show this help\n";
        std::cout << "  exit         - Exit program\n";
    }
};

int main() {
    CommandLineInterface cli;
    cli.run();
    return 0;
}
```

## Advanced Examples

### Example 4: Message Broadcasting

```cpp
// src/examples/broadcast_server.cpp
#include <iostream>
#include <chrono>
#include "Server.h"
#include "Log.h"

class BroadcastServer {
public:
    void start() {
        if (!server.start()) {
            log_error("Failed to start server");
            return;
        }
        
        log_info("Broadcast server started");
        
        // Set data handler
        server.setDataHandler([this](int clientId, const void* data, size_t length) {
            std::string message(reinterpret_cast<const char*>(data), length);
            log_info("Received from %d: %s", clientId, message.c_str());
            
            // Broadcast to all clients
            broadcast(message.c_str(), message.length());
        });
        
        // Start broadcast thread
        broadcastThread = std::thread(&BroadcastServer::broadcastLoop, this);
    }
    
    void stop() {
        server.stop();
        if (broadcastThread.joinable()) {
            broadcastThread.join();
        }
    }
    
private:
    Server server;
    std::thread broadcastThread;
    
    void broadcastLoop() {
        int counter = 0;
        while (server.isRunning()) {
            std::string message = "Server heartbeat " + std::to_string(counter++);
            
            // Broadcast to all connected clients
            int sent = server.broadcast(message.c_str(), message.length());
            log_info("Broadcast to %d clients: %s", sent, message.c_str());
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    void broadcast(const void* data, size_t length) {
        int sent = server.broadcast(data, length);
        if (sent == 0) {
            log_warning("No clients connected to broadcast to");
        }
    }
};

int main() {
    BroadcastServer server;
    server.start();
    
    // Wait for interrupt
    std::cout << "Press Enter to stop...\n";
    std::cin.ignore();
    
    server.stop();
    return 0;
}
```

### Example 5: Message Queue Management

```cpp
// src/examples/queue_manager.cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include "Client.h"
#include "Log.h"

class MessageQueue {
public:
    void push(const std::string& message) {
        std::unique_lock<std::mutex> lock(mutex);
        
        // Check queue size
        if (queue.size() >= maxQueueSize) {
            // Remove oldest message
            queue.pop();
            log_warning("Queue full, dropped oldest message");
        }
        
        queue.push(message);
        cv.notify_one();
    }
    
    std::string pop(int timeoutMs = -1) {
        std::unique_lock<std::mutex> lock(mutex);
        
        if (timeoutMs > 0) {
            if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
                           [this] { return !queue.empty(); })) {
                return "";  // Timeout
            }
        } else {
            cv.wait(lock, [this] { return !queue.empty(); });
        }
        
        std::string message = queue.front();
        queue.pop();
        return message;
    }
    
    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex);
        return queue.size();
    }
    
    void setMaxSize(size_t size) {
        std::unique_lock<std::mutex> lock(mutex);
        maxQueueSize = size;
    }
    
private:
    std::queue<std::string> queue;
    std::mutex mutex;
    std::condition_variable cv;
    size_t maxQueueSize = 1000;
};

class QueuedClient {
public:
    void connect(const std::string& address, int port) {
        if (!client.connect(address, port)) {
            log_error("Connection failed");
            return;
        }
        
        log_info("Connected to %s:%d", address.c_str(), port);
        
        // Set data handler
        client.setDataHandler([this](const void* data, size_t length) {
            std::string message(reinterpret_cast<const char*>(data), length);
            messageQueue.push(message);
        });
        
        // Start processing thread
        processingThread = std::thread(&QueuedClient::processMessages, this);
    }
    
    void send(const std::string& message) {
        if (client.isConnected()) {
            if (!client.sendUDPData(message.c_str(), message.length())) {
                log_error("Failed to send message");
            }
        } else {
            log_error("Not connected");
        }
    }
    
    void disconnect() {
        client.disconnect();
        if (processingThread.joinable()) {
            processingThread.join();
        }
    }
    
    void setMaxQueueSize(size_t size) {
        messageQueue.setMaxSize(size);
    }
    
private:
    Client client;
    MessageQueue messageQueue;
    std::thread processingThread;
    
    void processMessages() {
        while (client.isConnected()) {
            std::string message = messageQueue.pop(100);
            if (!message.empty()) {
                log_info("Processing: %s", message.c_str());
                
                // Process message here
                // For example, parse commands, etc.
            }
        }
    }
};

int main() {
    QueuedClient client;
    client.connect("localhost", 7001);
    client.setMaxQueueSize(500);
    
    // Send test messages
    for (int i = 0; i < 10; i++) {
        std::string message = "Test message " + std::to_string(i);
        client.send(message);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    client.disconnect();
    
    return 0;
}
```

### Example 6: Custom Protocol Handler

```cpp
// src/examples/custom_protocol.cpp
#include <iostream>
#include <map>
#include "Client.h"
#include "Log.h"

// Define custom message types
enum MessageType {
    MSG_TEXT = 1,
    MSG_BINARY = 2,
    MSG_PING = 3,
    MSG_PONG = 4
};

// Custom message header
#pragma pack(push, 1)
struct MessageHeader {
    uint8_t type;
    uint16_t length;
    uint16_t checksum;
};
#pragma pack(pop)

class ProtocolHandler {
public:
    void processMessage(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(MessageHeader)) {
            log_error("Invalid message: too small");
            return;
        }
        
        // Parse header
        const MessageHeader* header = 
            reinterpret_cast<const MessageHeader*>(data.data());
        
        // Verify checksum
        uint16_t calculated = calculateChecksum(
            data.data() + sizeof(MessageHeader), header->length);
        
        if (calculated != header->checksum) {
            log_error("Checksum mismatch");
            return;
        }
        
        // Process based on type
        switch (header->type) {
            case MSG_TEXT:
                processTextMessage(data);
                break;
            case MSG_BINARY:
                processBinaryMessage(data);
                break;
            case MSG_PING:
                handlePing();
                break;
            case MSG_PONG:
                handlePong();
                break;
            default:
                log_warning("Unknown message type: %d", header->type);
        }
    }
    
private:
    void processTextMessage(const std::vector<uint8_t>& data) {
        const MessageHeader* header = 
            reinterpret_cast<const MessageHeader*>(data.data());
        
        std::string text(
            reinterpret_cast<const char*>(data.data() + sizeof(MessageHeader)),
            header->length);
        
        log_info("Text message: %s", text.c_str());
    }
    
    void processBinaryMessage(const std::vector<uint8_t>& data) {
        const MessageHeader* header = 
            reinterpret_cast<const MessageHeader*>(data.data());
        
        log_info("Binary message: %d bytes", header->length);
        
        // Process binary data
        // ...
    }
    
    void handlePing() {
        log_info("Received PING");
        sendPong();
    }
    
    void handlePong() {
        log_info("Received PONG");
    }
    
    void sendPong() {
        MessageHeader header;
        header.type = MSG_PONG;
        header.length = 0;
        header.checksum = 0;
        
        std::vector<uint8_t> packet(sizeof(MessageHeader));
        memcpy(packet.data(), &header, sizeof(MessageHeader));
        
        // Send through client
        if (client) {
            client->sendUDPData(packet.data(), packet.size());
        }
    }
    
    uint16_t calculateChecksum(const void* data, size_t length) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
        uint16_t checksum = 0;
        
        for (size_t i = 0; i < length; i++) {
            checksum ^= bytes[i];
        }
        
        return checksum;
    }
    
public:
    void setClient(Client* c) {
        client = c;
    }
    
private:
    Client* client;
};

int main() {
    Client client;
    ProtocolHandler handler;
    handler.setClient(&client);
    
    // Set data handler to process messages through protocol handler
    client.setDataHandler([&handler](const void* data, size_t length) {
        std::vector<uint8_t> packet(
            reinterpret_cast<const uint8_t*>(data),
            reinterpret_cast<const uint8_t*>(data) + length);
        handler.processMessage(packet);
    });
    
    // Connect to server
    if (!client.connect("localhost", 7001)) {
        log_error("Connection failed");
        return 1;
    }
    
    // Send test messages
    sendTextMessage(client, "Hello from custom protocol!");
    sendBinaryMessage(client, std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04});
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    return 0;
}

// Helper functions
void sendTextMessage(Client& client, const std::string& text) {
    MessageHeader header;
    header.type = MSG_TEXT;
    header.length = static_cast<uint16_t>(text.length());
    header.checksum = calculateChecksum(text.c_str(), text.length());
    
    std::vector<uint8_t> packet(sizeof(MessageHeader) + text.length());
    memcpy(packet.data(), &header, sizeof(MessageHeader));
    memcpy(packet.data() + sizeof(MessageHeader), text.c_str(), text.length());
    
    client.sendUDPData(packet.data(), packet.size());
}

void sendBinaryMessage(Client& client, const std::vector<uint8_t>& data) {
    MessageHeader header;
    header.type = MSG_BINARY;
    header.length = static_cast<uint16_t>(data.size());
    header.checksum = calculateChecksum(data.data(), data.size());
    
    std::vector<uint8_t> packet(sizeof(MessageHeader) + data.size());
    memcpy(packet.data(), &header, sizeof(MessageHeader));
    memcpy(packet.data() + sizeof(MessageHeader), data.data(), data.size());
    
    client.sendUDPData(packet.data(), packet.size());
}

uint16_t calculateChecksum(const void* data, size_t length) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    uint16_t checksum = 0;
    
    for (size_t i = 0; i < length; i++) {
        checksum ^= bytes[i];
    }
    
    return checksum;
}
```

## Performance Examples

### Example 7: Throughput Test

```cpp
// src/examples/throughput_test.cpp
#include <iostream>
#include <chrono>
#include <vector>
#include "Client.h"
#include "Server.h"
#include "Log.h"

class ThroughputTest {
public:
    void runServer(int port) {
        Server server;
        
        server.setDataHandler([this, &server](int clientId, const void* data, size_t length) {
            // Echo back immediately
            server.sendToClient(clientId, data, length);
            
            // Track received data
            {
                std::lock_guard<std::mutex> lock(statsMutex);
                bytesReceived += length;
                messagesReceived++;
            }
        });
        
        if (!server.start()) {
            log_error("Failed to start server");
            return;
        }
        
        log_info("Throughput test server started on port %d", port);
        
        // Run for test duration
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() < testDuration) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Print stats every second
            printStats();
        }
    }
    
    void runClient(const std::string& address, int port, int messageSize, int ratePerSecond) {
        Client client;
        
        if (!client.connect(address, port)) {
            log_error("Connection failed");
            return;
        }
        
        log_info("Connected to %s:%d", address.c_str(), port);
        log_info("Sending %d bytes at %d messages/second", 
                messageSize, ratePerSecond);
        
        // Test parameters
        this->messageSize = messageSize;
        this->ratePerSecond = ratePerSecond;
        
        // Send test messages
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() < testDuration) {
            sendTestMessage();
            throttleSend();
            
            // Track sent data
            {
                std::lock_guard<std::mutex> lock(statsMutex);
                bytesSent += messageSize;
                messagesSent++;
            }
        }
        
        // Print final stats
        printStats();
    }
    
private:
    int messageSize = 1024;
    int ratePerSecond = 100;
    const int testDuration = 60;  // seconds
    
    int64_t bytesSent = 0;
    int64_t bytesReceived = 0;
    int messagesSent = 0;
    int messagesReceived = 0;
    
    std::mutex statsMutex;
    
    void sendTestMessage() {
        std::vector<uint8_t> data(messageSize, 'A');
        if (!client.sendUDPData(data.data(), data.size())) {
            log_error("Failed to send message");
        }
    }
    
    void throttleSend() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - lastSendTime).count();
        
        int targetInterval = 1000000 / ratePerSecond;  // microseconds
        
        if (elapsed < targetInterval) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(targetInterval - elapsed));
        }
        
        lastSendTime = std::chrono::steady_clock::now();
    }
    
    void printStats() {
        std::lock_guard<std::mutex> lock(statsMutex);
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - startTime).count();
        
        if (elapsed > 0) {
            double sendRate = bytesSent / (1024.0 * elapsed);  // KB/s
            double recvRate = bytesReceived / (1024.0 * elapsed);  // KB/s
            double msgRate = messagesSent / elapsed;
            
            std::cout << "After " << elapsed << "s:\n";
            std::cout << "  Sent: " << bytesSent << " bytes (" 
                      << sendRate << " KB/s)\n";
            std::cout << "  Received: " << bytesReceived << " bytes (" 
                      << recvRate << " KB/s)\n";
            std::cout << "  Messages sent: " << messagesSent << " (" 
                      << msgRate << "/s)\n";
            std::cout << "  Messages received: " << messagesReceived << "\n";
        }
    }
    
    Client client;
    std::chrono::steady_clock::time_point lastSendTime;
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage:\n";
        std::cout << "  throughput_test server [port]\n";
        std::cout << "  throughput_test client <address> <port> <size> <rate>\n";
        return 1;
    }
    
    std::string mode = argv[1];
    ThroughputTest test;
    
    if (mode == "server") {
        int port = argc > 2 ? std::stoi(argv[2]) : 7001;
        test.runServer(port);
    } else if (mode == "client") {
        if (argc < 6) {
            std::cout << "Missing parameters for client mode\n";
            return 1;
        }
        
        std::string address = argv[2];
        int port = std::stoi(argv[3]);
        int size = std::stoi(argv[4]);
        int rate = std::stoi(argv[5]);
        
        test.runClient(address, port, size, rate);
    } else {
        std::cout << "Unknown mode: " << mode << "\n";
        return 1;
    }
    
    return 0;
}
```

### Example 8: Latency Test

```cpp
// src/examples/latency_test.cpp
#include <iostream>
#include <chrono>
#include <vector>
#include "Client.h"
#include "Server.h"
#include "Log.h"

class LatencyTest {
public:
    struct LatencyStats {
        std::vector<int64_t> samples;
        int64_t min = INT64_MAX;
        int64_t max = INT64_MIN;
        double avg = 0.0;
        int64_t percentile95 = 0;
    };
    
    void runServer(int port) {
        Server server;
        
        // Map to track message IDs and their timestamps
        std::mutex idMutex;
        std::unordered_map<uint32_t, int64_t> messageTimestamps;
        
        server.setDataHandler([this, &server, &messageTimestamps, &idMutex]
            (int clientId, const void* data, size_t length) {
            
            // Parse message header
            if (length < sizeof(MessageHeader)) return;
            
            const MessageHeader* header = 
                reinterpret_cast<const MessageHeader*>(data);
            
            // For ping-pong test, just echo back
            if (header->type == MSG_PING) {
                server.sendToClient(clientId, data, length);
            }
            
            // For latency test, record timestamp
            if (header->type == MSG_LATENCY_TEST) {
                std::lock_guard<std::mutex> lock(idMutex);
                messageTimestamps[header->messageId] = 
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
            }
        });
        
        if (!server.start()) {
            log_error("Failed to start server");
            return;
        }
        
        log_info("Latency test server started on port %d", port);
        
        // Start cleanup thread
        cleanupThread = std::thread(&LatencyTest::cleanupTimestamps, 
                                   this, &messageTimestamps, &idMutex);
    }
    
    void runClient(const std::string& address, int port, int messageCount) {
        Client client;
        
        if (!client.connect(address, port)) {
            log_error("Connection failed");
            return;
        }
        
        log_info("Starting latency test to %s:%d", address.c_str(), port);
        log_info("Sending %d messages", messageCount);
        
        LatencyStats stats;
        std::mutex statsMutex;
        
        // Set response handler
        client.setDataHandler([this, &stats, &statsMutex]
            (const void* data, size_t length) {
            
            if (length < sizeof(MessageHeader)) return;
            
            const MessageHeader* header = 
                reinterpret_cast<const MessageHeader*>(data);
            
            if (header->type == MSG_LATENCY_TEST) {
                int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                
                int64_t latency = now - header->timestamp;
                
                std::lock_guard<std::mutex> lock(statsMutex);
                stats.samples.push_back(latency);
                stats.min = std::min(stats.min, latency);
                stats.max = std::max(stats.max, latency);
                stats.avg = (stats.avg * (stats.samples.size() - 1) + latency) / 
                           stats.samples.size();
                
                // Update percentile
                if (stats.samples.size() >= 20) {  // Enough samples
                    std::vector<int64_t> sorted = stats.samples;
                    std::sort(sorted.begin(), sorted.end());
                    int index = static_cast<int>(sorted.size() * 0.95);
                    stats.percentile95 = sorted[index];
                }
            }
        });
        
        // Send test messages
        for (uint32_t i = 0; i < messageCount; i++) {
            sendMessage(i);
            
            // Throttle to avoid overwhelming the network
            if (i % 100 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        // Wait for responses
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Print final stats
        std::lock_guard<std::mutex> lock(statsMutex);
        printLatencyStats(stats, messageCount);
    }
    
private:
    std::thread cleanupThread;
    
    enum MessageType {
        MSG_PING = 1,
        MSG_LATENCY_TEST = 2
    };
    
    struct MessageHeader {
        uint8_t type;
        uint32_t messageId;
        int64_t timestamp;
    };
    
    void sendMessage(uint32_t messageId) {
        MessageHeader header;
        header.type = MSG_LATENCY_TEST;
        header.messageId = messageId;
        header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        std::vector<uint8_t> packet(sizeof(MessageHeader));
        memcpy(packet.data(), &header, sizeof(MessageHeader));
        
        client.sendUDPData(packet.data(), packet.size());
    }
    
    void cleanupTimestamps(std::unordered_map<uint32_t, int64_t>* timestamps, 
                          std::mutex* mutex) {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            std::lock_guard<std::mutex> lock(*mutex);
            auto now = std::chrono::steady_clock::now();
            
            // Remove entries older than 1 minute
            for (auto it = timestamps->begin(); it != timestamps->end();) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - std::chrono::microseconds(it->second)).count();
                
                if (age > 60) {
                    it = timestamps->erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    
    void printLatencyStats(const LatencyStats& stats, int totalSent) {
        std::cout << "\nLatency Test Results:\n";
        std::cout << "Messages sent: " << totalSent << "\n";
        std::cout << "Responses received: " << stats.samples.size() << "\n";
        std::cout << "Success rate: " 
                  << (stats.samples.size() * 100.0 / totalSent) << "%\n";
        std::cout << "Min latency: " << stats.min << " μs\n";
        std::cout << "Max latency: " << stats.max << " μs\n";
        std::cout << "Average latency: " << stats.avg << " μs\n";
        std::cout << "95th percentile: " << stats.percentile95 << " μs\n";
        
        // Calculate jitter
        double jitter = 0.0;
        if (stats.samples.size() > 1) {
            for (size_t i = 1; i < stats.samples.size(); i++) {
                jitter += std::abs(stats.samples[i] - stats.samples[i-1]);
            }
            jitter /= (stats.samples.size() - 1);
        }
        std::cout << "Jitter: " << jitter << " μs\n";
    }
    
    Client client;
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage:\n";
        std::cout << "  latency_test server [port]\n";
        std::cout << "  latency_test client <address> <port> <count>\n";
        return 1;
    }
    
    std::string mode = argv[1];
    LatencyTest test;
    
    if (mode == "server") {
        int port = argc > 2 ? std::stoi(argv[2]) : 7001;
        test.runServer(port);
    } else if (mode == "client") {
        if (argc < 5) {
            std::cout << "Missing parameters for client mode\n";
            return 1;
        }
        
        std::string address = argv[2];
        int port = std::stoi(argv[3]);
        int count = std::stoi(argv[4]);
        
        test.runClient(address, port, count);
    } else {
        std::cout << "Unknown mode: " << mode << "\n";
        return 1;
    }
    
    return 0;
}
```

## Integration Examples

### Example 9: Integration with C++ Application

```cpp
// src/examples/integration_example.cpp
#include <iostream>
#include <thread>
#include <atomic>
#include "Client.h"
#include "Server.h"
#include "Log.h"

class Application {
public:
    void run() {
        initialize();
        
        // Start server in background
        serverThread = std::thread(&Application::runServer, this);
        
        // Run client interface
        runClientInterface();
        
        // Cleanup
        shutdown();
    }
    
private:
    Server server;
    Client client;
    std::thread serverThread;
    std::atomic<bool> running{true};
    
    void initialize() {
        // Set up server configuration
        ServerConfiguration serverConfig;
        serverConfig.tcpPort = 7001;
        serverConfig.maxConnections = 10;
        serverConfig.logLevel = INFO;
        
        server.setConfiguration(serverConfig);
        
        // Set up client configuration
        ClientConfiguration clientConfig;
        clientConfig.peerAddress = "localhost";
        clientConfig.peerTcpPort = 7001;
        clientConfig.localHostUdpPort = 5002;
        
        client.setConfiguration(clientConfig);
        
        // Set up callbacks
        setupCallbacks();
    }
    
    void setupCallbacks() {
        // Server callbacks
        server.setDataHandler([this](int clientId, const void* data, size_t length) {
            std::string message(reinterpret_cast<const char*>(data), length);
            std::cout << "[Server] Received from client " << clientId 
                      << ": " << message << std::endl;
            
            // Process message based on application logic
            processApplicationMessage(clientId, message);
        });
        
        server.setConnectionHandler([this](int clientId, bool connected) {
            if (connected) {
                std::cout << "[Server] Client " << clientId << " connected" << std::endl;
            } else {
                std::cout << "[Server] Client " << clientId << " disconnected" << std::endl;
            }
        });
        
        // Client callbacks
        client.setDataHandler([this](const void* data, size_t length) {
            std::string message(reinterpret_cast<const char*>(data), length);
            std::cout << "[Client] Received from server: " << message << std::endl;
        });
        
        client.setConnectionHandler([this](bool connected) {
            if (connected) {
                std::cout << "[Client] Connected to server" << std::endl;
                // Send initial message
                client.sendUDPData("Hello from application!", 22);
            } else {
                std::cout << "[Client] Disconnected from server" << std::endl;
            }
        });
    }
    
    void runServer() {
        if (!server.start()) {
            std::cerr << "Failed to start server" << std::endl;
            return;
        }
        
        std::cout << "Server started on port 7001" << std::endl;
        
        // Main server loop
        while (running) {
            // Check for connected clients
            int active = server.getActiveConnectionCount();
            if (active > 0) {
                std::cout << "Active connections: " << active << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Send periodic message to all clients
            std::string message = "Server time: " + 
                std::to_string(std::time(nullptr));
            int sent = server.broadcast(message.c_str(), message.length());
            if (sent > 0) {
                std::cout << "Broadcast to " << sent << " clients" << std::endl;
            }
        }
        
        server.stop();
    }
    
    void runClientInterface() {
        // Connect client
        if (!client.connect("localhost", 7001)) {
            std::cerr << "Failed to connect client" << std::endl;
            return;
        }
        
        std::cout << "Client interface started" << std::endl;
        
        // Command line interface
        std::string command;
        while (running) {
            std::cout << "app> ";
            std::getline(std::cin, command);
            
            if (command == "quit") {
                running = false;
            } else if (command == "status") {
                printStatus();
            } else if (command.substr(0, 4) == "send") {
                if (command.length() > 5) {
                    std::string message = command.substr(5);
                    client.sendUDPData(message.c_str(), message.length());
                }
            } else if (command == "help") {
                printHelp();
            }
        }
    }
    
    void processApplicationMessage(int clientId, const std::string& message) {
        // Example application logic
        if (message == "status") {
            std::string response = "Status: OK, connections: " + 
                std::to_string(server.getActiveConnectionCount());
            server.sendToClient(clientId, response.c_str(), response.length());
        } else if (message == "time") {
            std::string time = std::to_string(std::time(nullptr));
            server.sendToClient(clientId, time.c_str(), time.length());
        }
    }
    
    void printStatus() {
        std::cout << "Server status: " << (server.isRunning() ? "Running" : "Stopped") 
                  << std::endl;
        std::cout << "Client status: " << (client.isConnected() ? "Connected" : "Disconnected") 
                  << std::endl;
        std::cout << "Server connections: " << server.getActiveConnectionCount() 
                  << std::endl;
    }
    
    void printHelp() {
        std::cout << "Available commands:" << std::endl;
        std::cout << "  send <message> - Send message to server" << std::endl;
        std::cout << "  status         - Show system status" << std::endl;
        std::cout << "  time           - Get server time" << std::endl;
        std::cout << "  quit           - Exit application" << std::endl;
        std::cout << "  help           - Show this help" << std::endl;
    }
    
    void shutdown() {
        running = false;
        
        if (serverThread.joinable()) {
            serverThread.join();
        }
        
        client.disconnect();
        server.stop();
        
        std::cout << "Application shutdown complete" << std::endl;
    }
};

int main() {
    Application app;
    app.run();
    return 0;
}
```

## Real-world Scenarios

### Example 10: Chat Application

```cpp
// src/examples/chat_application.cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include "Client.h"
#include "Server.h"
#include "Log.h"

// Message types for chat application
enum ChatMessageType {
    MSG_CHAT = 1,
    MSG_JOIN = 2,
    MSG_LEAVE = 3,
    MSG_USER_LIST = 4,
    MSG_PRIVATE = 5
};

#pragma pack(push, 1)
struct ChatMessageHeader {
    uint8_t type;
    uint16_t length;
    uint32_t userId;
    char username[32];
};
#pragma pack(pop)

class ChatServer {
public:
    void start(int port) {
        if (!server.start()) {
            log_error("Failed to start chat server");
            return;
        }
        
        log_info("Chat server started on port %d", port);
        
        server.setDataHandler([this](int clientId, const void* data, size_t length) {
            handleClientMessage(clientId, data, length);
        });
        
        server.setConnectionHandler([this](int clientId, bool connected) {
            if (connected) {
                handleClientConnect(clientId);
            } else {
                handleClientDisconnect(clientId);
            }
        });
        
        // Start heartbeat thread
        heartbeatThread = std::thread(&ChatServer::heartbeatLoop, this);
    }
    
    void stop() {
        server.stop();
        if (heartbeatThread.joinable()) {
            heartbeatThread.join();
        }
    }
    
private:
    Server server;
    std::thread heartbeatThread;
    
    struct ClientInfo {
        std::string username;
        std::chrono::steady_clock::time_point lastActive;
    };
    
    std::map<int, ClientInfo> clients;
    std::mutex clientsMutex;
    
    void handleClientMessage(int clientId, const void* data, size_t length) {
        if (length < sizeof(ChatMessageHeader)) return;
        
        const ChatMessageHeader* header = 
            reinterpret_cast<const ChatMessageHeader*>(data);
        
        std::string message(
            reinterpret_cast<const char*>(data) + sizeof(ChatMessageHeader),
            header->length);
        
        // Update client activity
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients[clientId].lastActive = std::chrono::steady_clock::now();
        }
        
        // Process based on message type
        switch (header->type) {
            case MSG_CHAT:
                broadcastChatMessage(clientId, message);
                break;
            case MSG_JOIN:
                handleJoinMessage(clientId, std::string(header->username));
                break;
            case MSG_LEAVE:
                handleLeaveMessage(clientId);
                break;
            case MSG_PRIVATE:
                handlePrivateMessage(clientId, header->userId, message);
                break;
        }
    }
    
    void handleClientConnect(int clientId) {
        log_info("Client connected: %d", clientId);
        
        // Request username
        ChatMessageHeader header;
        header.type = MSG_JOIN;
        header.length = 0;
        header.userId = 0;
        memset(header.username, 0, sizeof(header.username));
        
        std::vector<uint8_t> packet(sizeof(ChatMessageHeader));
        memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
        
        server.sendToClient(clientId, packet.data(), packet.size());
    }
    
    void handleClientDisconnect(int clientId) {
        log_info("Client disconnected: %d", clientId);
        
        // Remove from client list
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            auto it = clients.find(clientId);
            if (it != clients.end()) {
                std::string username = it->second.username;
                clients.erase(it);
                
                // Notify other clients
                broadcastLeaveMessage(username);
            }
        }
    }
    
    void handleJoinMessage(int clientId, const std::string& username) {
        // Add client to list
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients[clientId] = {username, std::chrono::steady_clock::now()};
        }
        
        // Notify all clients
        broadcastJoinMessage(username);
        
        // Send user list to the new client
        sendUserList(clientId);
    }
    
    void handleLeaveMessage(int clientId) {
        // Client is leaving gracefully
        handleClientDisconnect(clientId);
    }
    
    void handlePrivateMessage(int fromClientId, uint32_t toUserId, const std::string& message) {
        // Find client by user ID (if we had a mapping)
        // For simplicity, we'll use the client ID directly
        
        // In a real implementation, you'd maintain a mapping from user ID to client ID
        std::string sender = getClientUsername(fromClientId);
        std::string privateMsg = "[PM from " + sender + "] " + message;
        
        // Send to the intended recipient
        // server.sendToClient(toClientId, ...)
        
        // For demo, we'll broadcast with PM marker
        broadcastChatMessage(fromClientId, privateMsg, true);
    }
    
    void broadcastChatMessage(int fromClientId, const std::string& message, bool isPrivate = false) {
        std::string sender = getClientUsername(fromClientId);
        
        ChatMessageHeader header;
        header.type = MSG_CHAT;
        header.length = static_cast<uint16_t>(message.length());
        header.userId = fromClientId;
        strncpy(header.username, sender.c_str(), sizeof(header.username) - 1);
        header.username[sizeof(header.username) - 1] = '\0';
        
        std::vector<uint8_t> packet(sizeof(ChatMessageHeader) + message.length());
        memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
        memcpy(packet.data() + sizeof(ChatMessageHeader), 
               message.c_str(), message.length());
        
        server.broadcast(packet.data(), packet.size());
    }
    
    void broadcastJoinMessage(const std::string& username) {
        ChatMessageHeader header;
        header.type = MSG_JOIN;
        header.length = static_cast<uint16_t>(username.length());
        header.userId = 0;
        strncpy(header.username, username.c_str(), sizeof(header.username) - 1);
        header.username[sizeof(header.username) - 1] = '\0';
        
        std::vector<uint8_t> packet(sizeof(ChatMessageHeader) + username.length());
        memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
        memcpy(packet.data() + sizeof(ChatMessageHeader), 
               username.c_str(), username.length());
        
        server.broadcast(packet.data(), packet.size());
    }
    
    void broadcastLeaveMessage(const std::string& username) {
        ChatMessageHeader header;
        header.type = MSG_LEAVE;
        header.length = static_cast<uint16_t>(username.length());
        header.userId = 0;
        strncpy(header.username, username.c_str(), sizeof(header.username) - 1);
        header.username[sizeof(header.username) - 1] = '\0';
        
        std::vector<uint8_t> packet(sizeof(ChatMessageHeader) + username.length());
        memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
        memcpy(packet.data() + sizeof(ChatMessageHeader), 
               username.c_str(), username.length());
        
        server.broadcast(packet.data(), packet.size());
    }
    
    void sendUserList(int clientId) {
        std::string userList = "Users online: ";
        
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (const auto& pair : clients) {
                if (pair.first != clientId) {
                    userList += pair.second.username + ", ";
                }
            }
        }
        
        // Remove trailing comma and space
        if (!userList.empty() && userList.back() == ' ') {
            userList.pop_back();
            userList.pop_back();
        }
        
        // Send to specific client
        ChatMessageHeader header;
        header.type = MSG_USER_LIST;
        header.length = static_cast<uint16_t>(userList.length());
        header.userId = 0;
        memset(header.username, 0, sizeof(header.username));
        
        std::vector<uint8_t> packet(sizeof(ChatMessageHeader) + userList.length());
        memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
        memcpy(packet.data() + sizeof(ChatMessageHeader), 
               userList.c_str(), userList.length());
        
        server.sendToClient(clientId, packet.data(), packet.size());
    }
    
    std::string getClientUsername(int clientId) {
        std::lock_guard<std::mutex> lock(clientsMutex);
        auto it = clients.find(clientId);
        if (it != clients.end()) {
            return it->second.username;
        }
        return "Unknown";
    }
    
    void heartbeatLoop() {
        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            // Check for inactive clients
            auto now = std::chrono::steady_clock::now();
            std::vector<int> inactiveClients;
            
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                for (const auto& pair : clients) {
                    auto age = std::chrono::duration_cast<std::chrono::seconds>(
                        now - pair.second.lastActive).count();
                    
                    if (age > 60) {  // 60 seconds inactive
                        inactiveClients.push_back(pair.first);
                    }
                }
            }
            
            // Remove inactive clients
            for (int clientId : inactiveClients) {
                log_info("Removing inactive client: %d", clientId);
                server.disconnectClient(clientId);
            }
        }
    }
};

class ChatClient {
public:
    void connect(const std::string& address, int port, const std::string& username) {
        if (!client.connect(address, port)) {
            log_error("Connection failed");
            return;
        }
        
        this->username = username;
        log_info("Connected to chat server as %s", username.c_str());
        
        // Send join message
        sendJoinMessage();
        
        // Start message processing thread
        messageThread = std::thread(&ChatClient::messageLoop, this);
    }
    
    void disconnect() {
        client.disconnect();
        if (messageThread.joinable()) {
            messageThread.join();
        }
    }
    
    void sendMessage(const std::string& message) {
        if (!message.empty()) {
            ChatMessageHeader header;
            header.type = MSG_CHAT;
            header.length = static_cast<uint16_t>(message.length());
            header.userId = 0;
            strncpy(header.username, username.c_str(), sizeof(header.username) - 1);
            header.username[sizeof(header.username) - 1] = '\0';
            
            std::vector<uint8_t> packet(sizeof(ChatMessageHeader) + message.length());
            memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
            memcpy(packet.data() + sizeof(ChatMessageHeader), 
                   message.c_str(), message.length());
            
            client.sendUDPData(packet.data(), packet.size());
        }
    }
    
    void run() {
        std::string input;
        std::cout << "Welcome to Chat! Type 'help' for commands.\n";
        
        while (true) {
            std::cout << username << "> ";
            std::getline(std::cin, input);
            
            if (input == "quit") {
                break;
            } else if (input == "help") {
                printHelp();
            } else if (input.substr(0, 7) == "pm user") {
                handlePrivateMessage(input);
            } else if (!input.empty()) {
                sendMessage(input);
            }
        }
    }
    
private:
    Client client;
    std::string username;
    std::thread messageThread;
    
    void messageLoop() {
        while (client.isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Process incoming messages
            // In a real implementation, you'd have a proper message queue
        }
    }
    
    void sendJoinMessage() {
        ChatMessageHeader header;
        header.type = MSG_JOIN;
        header.length = static_cast<uint16_t>(username.length());
        header.userId = 0;
        strncpy(header.username, username.c_str(), sizeof(header.username) - 1);
        header.username[sizeof(header.username) - 1] = '\0';
        
        std::vector<uint8_t> packet(sizeof(ChatMessageHeader) + username.length());
        memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
        memcpy(packet.data() + sizeof(ChatMessageHeader), 
               username.c_str(), username.length());
        
        client.sendUDPData(packet.data(), packet.size());
    }
    
    void handlePrivateMessage(const std::string& input) {
        // Parse private message format: "pm user message"
        size_t spacePos = input.find(' ', 4);
        if (spacePos != std::string::npos) {
            std::string target = input.substr(4, spacePos - 4);
            std::string message = input.substr(spacePos + 1);
            
            // Create private message header
            ChatMessageHeader header;
            header.type = MSG_PRIVATE;
            header.length = static_cast<uint16_t>(message.length());
            header.userId = std::hash<std::string>{}(target);  // Simple hash for demo
            strncpy(header.username, username.c_str(), sizeof(header.username) - 1);
            header.username[sizeof(header.username) - 1] = '\0';
            
            std::vector<uint8_t> packet(sizeof(ChatMessageHeader) + message.length());
            memcpy(packet.data(), &header, sizeof(ChatMessageHeader));
            memcpy(packet.data() + sizeof(ChatMessageHeader), 
                   message.c_str(), message.length());
            
            client.sendUDPData(packet.data(), packet.size());
        }
    }
    
    void printHelp() {
        std::cout << "\nChat Commands:\n";
        std::cout << "  [message]        - Send message to all\n";
        std::cout << "  pm user message  - Send private message\n";
        std::cout << "  help             - Show this help\n";
        std::cout << "  quit             - Exit chat\n\n";
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage:\n";
        std::cout << "  chat_server [port]\n";
        std::cout << "  chat_client <address> <port> <username>\n";
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "chat_server") {
        int port = argc > 2 ? std::stoi(argv[2]) : 7001;
        ChatServer server;
        server.start(port);
        
        std::cout << "Press Enter to stop server...\n";
        std::cin.ignore();
        
        server.stop();
    } else if (mode == "chat_client") {
        if (argc < 5) {
            std::cout << "Missing username for client\n";
            return 1;
        }
        
        std::string address = argv[2];
        int port = std::stoi(argv[3]);
        std::string username = argv[4];
        
        ChatClient client;
        client.connect(address, port, username);
        client.run();
        client.disconnect();
    } else {
        std::cout << "Unknown mode: " << mode << "\n";
        return 1;
    }
    
    return 0;
}
```

These examples demonstrate various ways to use the TCP/UDP project in different scenarios, from simple echo servers to complex chat applications. Each example includes proper error handling, logging, and follows the project's architecture patterns.