#include "gtest/gtest.h"
#include "UdpToTcpSocketMap.h"

class UdpToTcpSocketMapTest : public ::testing::Test {
protected:
    UdpToTcpSocketMap& socketMap = UdpToTcpSocketMap::getInstance();

    void SetUp() override {
        // Clear the socket map before each test
        socketMap.Reset();
    }
};

TEST_F(UdpToTcpSocketMapTest, MapSockets) {
    int udpSocket = 1;
    int tcpSocket1 = 100;
    int tcpSocket2 = 101;

    socketMap.mapSockets(udpSocket, tcpSocket1);
    socketMap.mapSockets(udpSocket, tcpSocket2);

    EXPECT_EQ(socketMap.retrieveMappedTcpSocket(udpSocket), tcpSocket1);
    EXPECT_EQ(socketMap.retrieveMappedTcpSocket(udpSocket), tcpSocket2);
    EXPECT_EQ(socketMap.retrieveMappedTcpSocket(udpSocket), tcpSocket1);
}

TEST_F(UdpToTcpSocketMapTest, RetrieveMappedTcpSocketRoundRobin) {
    int udpSocket = 2;
    int tcpSocket1 = 200;
    int tcpSocket2 = 201;
    int tcpSocket3 = 202;

    socketMap.mapSockets(udpSocket, tcpSocket1);
    socketMap.mapSockets(udpSocket, tcpSocket2);
    socketMap.mapSockets(udpSocket, tcpSocket3);

    EXPECT_EQ(socketMap.retrieveMappedTcpSocket(udpSocket), tcpSocket1);
    EXPECT_EQ(socketMap.retrieveMappedTcpSocket(udpSocket), tcpSocket2);
    EXPECT_EQ(socketMap.retrieveMappedTcpSocket(udpSocket), tcpSocket3);
    EXPECT_EQ(socketMap.retrieveMappedTcpSocket(udpSocket), tcpSocket1);
}
