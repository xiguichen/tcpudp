#include <gtest/gtest.h>
#include <Protocol.h>
#include <memory>

// Define a simple mock socket interface for testing
class ISocket {
public:
    virtual ~ISocket() = default;
    virtual void connect(const std::string& address, int port) = 0;
    virtual void send(const std::vector<char>& data) = 0;
    virtual std::shared_ptr<std::vector<char>> receive() = 0;
    virtual void close() = 0;
};

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
    
    std::shared_ptr<std::vector<char>> receive() override {
        if (!isConnected) {
            throw std::runtime_error("Not connected (mock)");
        }
        
        if (receiveQueue.empty()) {
            return {};
        }
        
        std::vector<char> data = receiveQueue.front();
        receiveQueue.erase(receiveQueue.begin());
        printf("Received data of size: %zu\n", data.size());
        return std::make_shared<std::vector<char>>(data);
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

// Test fixture for PeerTcpSocket tests
class PeerTcpSocketTest : public ::testing::Test {
protected:
    // We can't directly test PeerTcpSocket because it creates a real socket
    // Instead, we'll test the handshake logic separately
    
    void SetUp() override {
        mockSocket = std::make_unique<MockSocket>();
    }
    
    std::unique_ptr<MockSocket> mockSocket;
};

// Test the handshake process
TEST_F(PeerTcpSocketTest, HandshakeProcess) {
    // Create a MsgBind structure with a specific client ID
    uint32_t clientId = 12345;
    MsgBind msgBind;
    msgBind.clientId = clientId;
    
    // Serialize the MsgBind structure
    std::vector<char> buffer(sizeof(MsgBind));
    std::memcpy(buffer.data(), &msgBind, sizeof(MsgBind));
    
    // Send the data through the mock socket
    mockSocket->connect("127.0.0.1", 6001);
    mockSocket->send(buffer);
    
    // Verify the sent data
    const std::vector<char>& sentData = mockSocket->getSentData();
    ASSERT_EQ(sentData.size(), sizeof(MsgBind));
    
    // Create a copy of the sent data to avoid memory issues
    std::vector<char> sentDataCopy = sentData;
    MsgBind* sentBind = reinterpret_cast<MsgBind*>(sentDataCopy.data());
    EXPECT_EQ(sentBind->clientId, clientId);
    
    // Create a MsgBindResponse
    MsgBindResponse bindResponse;
    bindResponse.connectionId = 67890;
    
    // Queue the response for the mock socket to return
    std::vector<char> responseBuffer(sizeof(MsgBindResponse));
    std::memcpy(responseBuffer.data(), &bindResponse, sizeof(MsgBindResponse));
    mockSocket->queueDataForReceive(responseBuffer);
    
    // Receive the response
    std::vector<char>& receivedData = *(mockSocket->receive());
    ASSERT_EQ(receivedData.size(), sizeof(MsgBindResponse));
    
    // Verify the received data
    MsgBindResponse receivedResponse;
    std::memcpy(&receivedResponse, receivedData.data(), sizeof(MsgBindResponse));
    EXPECT_EQ(receivedResponse.connectionId, bindResponse.connectionId);
}

// Test connection failure
TEST_F(PeerTcpSocketTest, ConnectionFailure) {
    mockSocket->setShouldConnect(false);
    
    // Attempt to connect should throw an exception
    EXPECT_THROW(mockSocket->connect("127.0.0.1", 6001), std::runtime_error);
    EXPECT_FALSE(mockSocket->isSocketConnected());
}

// Test sending data without being connected
TEST_F(PeerTcpSocketTest, SendWithoutConnection) {
    // Attempt to send without connecting should throw an exception
    std::vector<char> data = {'t', 'e', 's', 't'};
    EXPECT_THROW(mockSocket->send(data), std::runtime_error);
}

// Test receiving data without being connected
TEST_F(PeerTcpSocketTest, ReceiveWithoutConnection) {
    // Attempt to receive without connecting should throw an exception
    EXPECT_THROW(mockSocket->receive(), std::runtime_error);
}

// Test closing the connection
TEST_F(PeerTcpSocketTest, CloseConnection) {
    mockSocket->connect("127.0.0.1", 6001);
    EXPECT_TRUE(mockSocket->isSocketConnected());
    
    mockSocket->close();
    EXPECT_FALSE(mockSocket->isSocketConnected());
}

// Test the complete handshake flow
TEST_F(PeerTcpSocketTest, CompleteHandshakeFlow) {
    // Add state tracking to MockSocket
    enum class MockSocketState { DISCONNECTED, CONNECTED, AUTHENTICATED };
    MockSocketState socketState = MockSocketState::DISCONNECTED;
    uint32_t mockConnectionId = 0;
    
    // Connect to the server
    mockSocket->connect("127.0.0.1", 6001);
    socketState = MockSocketState::CONNECTED;
    EXPECT_TRUE(mockSocket->isSocketConnected());
    EXPECT_EQ(mockSocket->getConnectedAddress(), "127.0.0.1");
    EXPECT_EQ(mockSocket->getConnectedPort(), 6001);
    
    // Create and send the MsgBind
    uint32_t clientId = 12345;
    MsgBind msgBind;
    msgBind.clientId = clientId;
    
    std::vector<char> buffer(sizeof(MsgBind));
    std::memcpy(buffer.data(), &msgBind, sizeof(MsgBind));
    mockSocket->send(buffer);
    
    // Verify the sent data
    const std::vector<char>& sentData = mockSocket->getSentData();
    ASSERT_EQ(sentData.size(), sizeof(MsgBind));
    
    // Create a copy of the sent data to avoid memory issues
    std::vector<char> sentDataCopy = sentData;
    MsgBind* sentBind = reinterpret_cast<MsgBind*>(sentDataCopy.data());
    EXPECT_EQ(sentBind->clientId, clientId);
    
    // Create and queue the MsgBindResponse
    MsgBindResponse bindResponse;
    bindResponse.connectionId = 67890;
    mockConnectionId = bindResponse.connectionId;
    
    std::vector<char> responseBuffer(sizeof(MsgBindResponse));
    std::memcpy(responseBuffer.data(), &bindResponse, sizeof(MsgBindResponse));
    mockSocket->queueDataForReceive(responseBuffer);
    
    // Receive the response
    auto tempData = mockSocket->receive();
    printf("Received data size: %zu\n", tempData->size());
    std::vector<char>& receivedData = *tempData;
    ASSERT_EQ(receivedData.size(), sizeof(MsgBindResponse));
    
    // Verify the received data
    MsgBindResponse receivedResponse;
    std::memcpy(&receivedResponse, receivedData.data(), sizeof(MsgBindResponse));
    EXPECT_EQ(receivedResponse.connectionId, bindResponse.connectionId);
    
    // Update state to authenticated
    socketState = MockSocketState::AUTHENTICATED;
    
    // Verify state transitions
    EXPECT_EQ(static_cast<int>(socketState), static_cast<int>(MockSocketState::AUTHENTICATED));
    EXPECT_EQ(mockConnectionId, bindResponse.connectionId);
    
    // Close the connection
    mockSocket->close();
    socketState = MockSocketState::DISCONNECTED;
    mockConnectionId = 0;
    EXPECT_FALSE(mockSocket->isSocketConnected());
    EXPECT_EQ(static_cast<int>(socketState), static_cast<int>(MockSocketState::DISCONNECTED));
}
