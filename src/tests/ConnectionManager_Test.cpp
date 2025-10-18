#include <gtest/gtest.h>
#include <Protocol.h>
#include <set>

// Test fixture for ConnectionManager tests
class ConnectionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset the ConnectionManager singleton for each test
        // This is a bit of a hack, but necessary for testing singletons
        ConnectionManager::getInstance().removeAllConnections();
    }
    
    void TearDown() override {
        // Clean up after each test
        ConnectionManager::getInstance().removeAllConnections();
    }
};

// Test creating a connection
TEST_F(ConnectionManagerTest, CreateConnection) {
    uint32_t clientId = 12345;
    
    // Create a connection
    Connection* connection = ConnectionManager::getInstance().createConnection(clientId);
    
    // Verify the connection
    ASSERT_NE(connection, nullptr);
    EXPECT_EQ(connection->clientId, clientId);
    EXPECT_NE(connection->connectionId, 0);
}

// Test removing a connection
TEST_F(ConnectionManagerTest, RemoveConnection) {
    uint32_t clientId = 12345;
    
    // Create a connection
    Connection* connection = ConnectionManager::getInstance().createConnection(clientId);
    uint32_t connectionId = connection->connectionId;
    
    // Remove the connection
    bool result = ConnectionManager::getInstance().removeConnection(connectionId);
    
    // Verify the result
    EXPECT_TRUE(result);
    
    // Try to remove the same connection again
    result = ConnectionManager::getInstance().removeConnection(connectionId);
    
    // Verify the result
    EXPECT_FALSE(result);
}

// Test creating multiple connections
TEST_F(ConnectionManagerTest, CreateMultipleConnections) {
    uint32_t clientId1 = 12345;
    uint32_t clientId2 = 67890;
    
    // Create connections
    Connection* connection1 = ConnectionManager::getInstance().createConnection(clientId1);
    Connection* connection2 = ConnectionManager::getInstance().createConnection(clientId2);
    
    // Verify the connections
    ASSERT_NE(connection1, nullptr);
    ASSERT_NE(connection2, nullptr);
    EXPECT_EQ(connection1->clientId, clientId1);
    EXPECT_EQ(connection2->clientId, clientId2);
    EXPECT_NE(connection1->connectionId, connection2->connectionId);
}

// Test connection ID uniqueness
TEST_F(ConnectionManagerTest, ConnectionIdUniqueness) {
    std::set<uint32_t> connectionIds;
    
    // Create multiple connections
    for (uint32_t i = 0; i < 100; ++i) {
        Connection* connection = ConnectionManager::getInstance().createConnection(i);
        
        // Verify the connection ID is unique
        ASSERT_NE(connection, nullptr);
        EXPECT_EQ(connectionIds.count(connection->connectionId), 0);
        
        // Add the connection ID to the set
        connectionIds.insert(connection->connectionId);
    }
    
    // Verify the number of unique connection IDs
    EXPECT_EQ(connectionIds.size(), 100);
}