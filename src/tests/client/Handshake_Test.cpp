#include <gtest/gtest.h>
#include <Protocol.h>
#include <memory>
#include <vector>
#include <algorithm>

// Mock socket class for testing
class SimpleMockSocket {
public:
    std::vector<char> sentData;
    std::vector<char> receiveData;
    bool connected = true;
    int errorCode = 0;
    
    ssize_t send(const void* data, size_t size) {
        if (!connected) return -1;
        
        const char* charData = static_cast<const char*>(data);
        sentData.insert(sentData.end(), charData, charData + size);
        return size;
    }
    
    ssize_t receive(void* buffer, size_t size) {
        if (!connected) return -1;
        if (receiveData.empty()) return 0;
        
        size_t copySize = std::min(size, receiveData.size());
        std::memcpy(buffer, receiveData.data(), copySize);
        
        // Remove the copied data from the receive buffer
        receiveData.erase(receiveData.begin(), receiveData.begin() + copySize);
        
        return copySize;
    }
};

// Test fixture for handshake tests
class HandshakeTest : public ::testing::Test {
protected:
    std::unique_ptr<SimpleMockSocket> mockSocket;
    
    void SetUp() override {
        mockSocket = std::make_unique<SimpleMockSocket>();
    }
};

// Test sending MsgBind
TEST_F(HandshakeTest, SendMsgBind) {
    // Create a MsgBind structure
    MsgBind msgBind;
    msgBind.clientId = 12345;
    
    // Serialize the MsgBind structure
    std::vector<char> buffer(sizeof(MsgBind));
    std::memcpy(buffer.data(), &msgBind, sizeof(MsgBind));
    
    // Send the data through the mock socket
    ssize_t result = mockSocket->send(buffer.data(), buffer.size());
    
    // Verify the result
    EXPECT_EQ(result, sizeof(MsgBind));
    EXPECT_EQ(mockSocket->sentData.size(), sizeof(MsgBind));
    
    // Verify the sent data
    MsgBind* sentBind = reinterpret_cast<MsgBind*>(mockSocket->sentData.data());
    EXPECT_EQ(sentBind->clientId, msgBind.clientId);
}

// Test receiving MsgBindResponse
TEST_F(HandshakeTest, ReceiveMsgBindResponse) {
    // Create a MsgBindResponse structure
    MsgBindResponse bindResponse;
    bindResponse.connectionId = 67890;
    
    // Add the response to the mock socket's receive buffer
    mockSocket->receiveData.resize(sizeof(MsgBindResponse));
    std::memcpy(mockSocket->receiveData.data(), &bindResponse, sizeof(MsgBindResponse));
    
    // Receive the data
    std::vector<char> buffer(sizeof(MsgBindResponse));
    ssize_t result = mockSocket->receive(buffer.data(), buffer.size());
    
    // Verify the result
    EXPECT_EQ(result, sizeof(MsgBindResponse));
    
    // Verify the received data
    MsgBindResponse receivedResponse;
    std::memcpy(&receivedResponse, buffer.data(), sizeof(MsgBindResponse));
    EXPECT_EQ(receivedResponse.connectionId, bindResponse.connectionId);
}

// Test complete handshake process
TEST_F(HandshakeTest, CompleteHandshake) {
    // Create a MsgBind structure
    MsgBind msgBind;
    msgBind.clientId = 12345;
    
    // Serialize the MsgBind structure
    std::vector<char> sendBuffer(sizeof(MsgBind));
    std::memcpy(sendBuffer.data(), &msgBind, sizeof(MsgBind));
    
    // Send the data through the mock socket
    ssize_t sendResult = mockSocket->send(sendBuffer.data(), sendBuffer.size());
    EXPECT_EQ(sendResult, sizeof(MsgBind));
    
    // Create a MsgBindResponse structure
    MsgBindResponse bindResponse;
    bindResponse.connectionId = 67890;
    
    // Add the response to the mock socket's receive buffer
    mockSocket->receiveData.resize(sizeof(MsgBindResponse));
    std::memcpy(mockSocket->receiveData.data(), &bindResponse, sizeof(MsgBindResponse));
    
    // Receive the data
    std::vector<char> receiveBuffer(sizeof(MsgBindResponse));
    ssize_t receiveResult = mockSocket->receive(receiveBuffer.data(), receiveBuffer.size());
    EXPECT_EQ(receiveResult, sizeof(MsgBindResponse));
    
    // Verify the sent data
    MsgBind* sentBind = reinterpret_cast<MsgBind*>(mockSocket->sentData.data());
    EXPECT_EQ(sentBind->clientId, msgBind.clientId);
    
    // Verify the received data
    MsgBindResponse receivedResponse;
    std::memcpy(&receivedResponse, receiveBuffer.data(), sizeof(MsgBindResponse));
    EXPECT_EQ(receivedResponse.connectionId, bindResponse.connectionId);
}

// Test handshake with connection failure
TEST_F(HandshakeTest, HandshakeConnectionFailure) {
    // Disconnect the mock socket
    mockSocket->connected = false;
    
    // Create a MsgBind structure
    MsgBind msgBind;
    msgBind.clientId = 12345;
    
    // Serialize the MsgBind structure
    std::vector<char> buffer(sizeof(MsgBind));
    std::memcpy(buffer.data(), &msgBind, sizeof(MsgBind));
    
    // Try to send the data through the mock socket
    ssize_t result = mockSocket->send(buffer.data(), buffer.size());
    
    // Verify the result indicates failure
    EXPECT_EQ(result, -1);
}