#include "Log.h"
#include "Socket.h"
#include "TcpVirtualChannel.h"
#include <condition_variable>
#include <gtest/gtest.h>
#include <cerrno>
#include <cstring>

// Test fixture for TcpVirtualChannel
class TcpVirtualChannelTest : public ::testing::Test
{
  protected:
    std::shared_ptr<TcpVirtualChannel> clientChannel;
    std::shared_ptr<TcpVirtualChannel> serverChannel;
    SocketFd serverSocket;
    SocketFd clientSocket;
    SocketFd serverAcceptedSocket;
    const char *serverAddrStr = "127.0.0.1";
    int serverPort = 0;
    ;
    std::mutex serverMutex;
    std::condition_variable serverCondition;
    bool serverReady = false;

    void SetUp() override
    {

#ifdef _WIN32
        // Initialize Winsock
        WSAData wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        std::thread serverThread([this]() { SetupServerSocket(); });
        std::thread clientThread([this]() { SetupClientSocket(); });

        serverThread.join();
        clientThread.join();
    }

    void SetupServerSocket()
    {
                    std::cout << "[DEBUG] Starting: Setting up server socket..." << std::endl;
        serverSocket = SocketCreate(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
        std::cout << "Failed to create server socket." << std::endl;
            return;
        }
        SocketReuseAddress(serverSocket);
        sockaddr_in serverAddr{};
    #if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        serverAddr.sin_len = sizeof(serverAddr);
    #endif
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(serverAddrStr);
        serverAddr.sin_port = htons(serverPort);
        auto result = SocketBind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
        if (result != 0)
        {
            std::cout << "SocketBind failed: errno=" << errno << " (" << std::strerror(errno) << ")" << std::endl;
            {
                std::unique_lock<std::mutex> lock(serverMutex);
                serverReady = true;
                serverCondition.notify_all();
            }
            ASSERT_EQ(result, 0);
            return;
        }

        sockaddr_in boundAddr;
        socklen_t boundAddrLen = sizeof(boundAddr);
        if (getsockname(serverSocket, (sockaddr *)&boundAddr, &boundAddrLen) == 0)
        {
            serverPort = ntohs(boundAddr.sin_port);
        }

        std::cout << "Server listening on port " << serverPort << "..." << std::endl;
        SocketListen(serverSocket, 1);

        // Notify that server is ready to accept connections
        {
            std::unique_lock<std::mutex> lock(serverMutex);
            serverReady = true;
            serverCondition.notify_all();
        }

        // Accept a connection
        sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);

        std::cout << "Server waiting to accept connection..." << std::endl;
        serverAcceptedSocket = SocketAccept(serverSocket, (sockaddr *)&clientAddr, &clientAddrLen);
        serverChannel = std::make_shared<TcpVirtualChannel>(std::vector<SocketFd>{serverAcceptedSocket});

        SocketClose(serverSocket); // Close the listening socket as it's no longer needed
        std::cout << "Server accepted connection from client." << std::endl;
    }

    void SetupClientSocket()
    {

        std::cout << "Setting up client socket..." << std::endl;

        // Wait until the server is ready to listen
        {
            std::unique_lock<std::mutex> lock(serverMutex);
            serverCondition.wait(lock, [this] { return serverReady; });
        }

        clientSocket = SocketCreate(AF_INET, SOCK_STREAM, 0);
        sockaddr_in serverAddr{};
    #if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        serverAddr.sin_len = sizeof(serverAddr);
    #endif
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(serverAddrStr);
        serverAddr.sin_port = htons(serverPort);
        std::cout << "Client connecting to server at " << serverAddrStr << ":" << serverPort << "..." << std::endl;
        auto result = SocketConnect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
        ASSERT_EQ(result, 0);
        std::cout << "Client connected to server socket." << std::endl;

        clientChannel = std::make_shared<TcpVirtualChannel>(std::vector<SocketFd>{clientSocket});

        std::cout << "Client connected to server." << std::endl;
    }

    void TearDown() override
    {
        log_info("TearDown started");
        if (clientChannel && clientChannel->isOpen())
        {
            log_info("Closing client channel");
            clientChannel->close();
        }
        clientChannel.reset();
        if (serverChannel && serverChannel->isOpen())
        {
            log_info("Closing server channel");
            serverChannel->close();
        }
        serverChannel.reset();
        log_info("Channels closed successfully");

#ifdef _WIN32
        WSACleanup();
        log_info("WSA cleanup completed");
#endif
        log_info("TearDown completed");
    }
};

// Test send method
TEST_F(TcpVirtualChannelTest, SendRecvTest)
{
    const char *data = "test data";
    size_t size = strlen(data) + 1;

    // recv data callback
    std::atomic<bool> callbackCalled = false;
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;

    auto recvCallback = [&callbackCalled, &callbackMutex, &callbackCondition, this, data, size](const char *recvData,
                                                                                                size_t recvSize) {
        EXPECT_EQ(recvSize, size);
        EXPECT_STREQ(recvData, data);
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbackCalled = true;
        }
        callbackCondition.notify_one();
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // server thread to wait for callback
    std::thread serverThread([&callbackCalled, &callbackMutex, &callbackCondition]() {
        std::unique_lock<std::mutex> lock(callbackMutex);
        if (!callbackCondition.wait_for(lock, std::chrono::seconds(5), [&callbackCalled]() { return callbackCalled.load(); })) {
            log_info("Timeout waiting for callback signal");
            return; // Timeout prevents indefinite blocking
        }
    });

    // client sends data
    std::thread clientThread([this, data, size]() { clientChannel->send(data, size); });

    clientThread.join();
    serverThread.join();
}

// Send and Receive multiple times
TEST_F(TcpVirtualChannelTest, SendRecvTest2)
{
    std::atomic<int> callbackCount = 0;
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;

    const char *data = "test data";
    size_t size = strlen(data) + 1;

    const char *data2 = "test data 2";
    size_t size2 = strlen(data2) + 1;

    static int callCount = 0;

    // recv data callback
    auto recvCallback = [&callbackCount, &callbackMutex, &callbackCondition, this, data, size, data2,
                         size2](const char *recvData, size_t recvSize) {
        if (callCount == 0)
        {
            EXPECT_EQ(recvSize, size);
            EXPECT_STREQ(recvData, data);
        }
        else if (callCount == 1)
        {
            EXPECT_EQ(recvSize, size2);
            EXPECT_STREQ(recvData, data2);
        }
        else
        {
            FAIL() << "Callback called more than twice";
        }
        callCount++;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbackCount++;
        }
        callbackCondition.notify_all();
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // server thread to wait for callback
    std::thread serverThread([&callbackCount, &callbackMutex, &callbackCondition]() {
        {
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&callbackCount]() { return callbackCount.load() >= 1; });
        }
        {
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&callbackCount]() { return callbackCount.load() >= 2; });
        }
    });

    // client sends data
    std::thread clientThread([this, data, size, data2, size2]() {
        clientChannel->send(data, size);
        clientChannel->send(data2, size2);
    });

    clientThread.join();
    serverThread.join();
    ASSERT_EQ(callCount, 2);
}

// // Check if we can handle out-of-order messages
TEST_F(TcpVirtualChannelTest, processReceivedDataTest)
{

    std::atomic<int> callbackCount = 0;
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;

    const char *data1 = "message 1";
    size_t size1 = strlen(data1) + 1;

    const char *data2 = "message 2";
    size_t size2 = strlen(data2) + 1;

    const char *data3 = "message 3";
    size_t size3 = strlen(data3) + 1;

    static int callCount = 0;

    // recv data callback
    // Packets arrive out-of-order (2, 0, 1) but reorder logic delivers them in sequence (0, 1, 2)
    auto recvCallback = [&callbackCount, &callbackMutex, &callbackCondition, this, data1, size1, data2, size2, data3,
                         size3](const char *recvData, size_t recvSize) {
        if (callCount == 0)
        {
            EXPECT_EQ(recvSize, size1);
            EXPECT_STREQ(recvData, data1);
        }
        else if (callCount == 1)
        {
            EXPECT_EQ(recvSize, size2);
            EXPECT_STREQ(recvData, data2);
        }
        else if (callCount == 2)
        {
            EXPECT_EQ(recvSize, size3);
            EXPECT_STREQ(recvData, data3);
        }
        else
        {
            {
                auto message = std::string("Unexpected callback count: ") + std::to_string(callCount);
                std::cout << message << std::endl;
            }
            FAIL() << "Callback called more than three times";
        }
        callCount++;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbackCount++;
        }
        callbackCondition.notify_all();
        std::cout << "Callback called for message " << callCount << std::endl;
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // server thread to wait for callback
    std::thread serverThread([&callbackCount, &callbackMutex, &callbackCondition]()
    {
        std::cout << "Server thread started, waiting for callbacks..." << std::endl;
        for (int i = 0; i < 3; i++)
        {
            std::cout << "Server thread waiting for callback " << (i + 1) << std::endl;
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&callbackCount, i]() { return callbackCount.load() >= (i + 1); });
            std::cout << "Server thread received callback " << (i + 1) << std::endl;
        }
        std::cout << "Server thread finished processing all callbacks" << std::endl;
    });

    // Simulate out-of-order message reception
    std::thread clientThread([this, data1, size1, data2, size2, data3, size3]()
    {
        // instead of sending via channel, directly call processReceivedData to simulate out-of-order
        log_info("Simulating out-of-order message reception");
        serverChannel->processReceivedData(2, std::make_shared<std::vector<char>>(data3, data3 + size3), 0);
        serverChannel->processReceivedData(0, std::make_shared<std::vector<char>>(data1, data1 + size1), 0);
        serverChannel->processReceivedData(1, std::make_shared<std::vector<char>>(data2, data2 + size2), 0);
        std::cout << "Client thread finished simulating out-of-order messages" << std::endl;
    });

    clientThread.join();
    serverThread.join();
    ASSERT_EQ(callCount, 3);
}

// Test that duplicate messages are dropped
TEST_F(TcpVirtualChannelTest, DuplicateMessageDropTest)
{
    std::atomic<int> callbackCount = 0;

    const char *data1 = "message 1";
    size_t size1 = strlen(data1) + 1;

    auto recvCallback = [&callbackCount, data1, size1](const char *recvData, size_t recvSize) {
        EXPECT_EQ(recvSize, size1);
        EXPECT_STREQ(recvData, data1);
        callbackCount++;
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // Send same messageId=0 twice
    serverChannel->processReceivedData(0, std::make_shared<std::vector<char>>(data1, data1 + size1), 0);
    serverChannel->processReceivedData(0, std::make_shared<std::vector<char>>(data1, data1 + size1), 0);

    // Wait for async delivery thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Only the first should have been delivered
    ASSERT_EQ(callbackCount.load(), 1);
}

// Test that messages older than nextMessageId are dropped
TEST_F(TcpVirtualChannelTest, OldMessageDropTest)
{
    std::atomic<int> callbackCount = 0;

    const char *data1 = "message 1";
    size_t size1 = strlen(data1) + 1;

    const char *data2 = "message 2";
    size_t size2 = strlen(data2) + 1;

    const char *dataOld = "old message";
    size_t sizeOld = strlen(dataOld) + 1;

    int localCallCount = 0;
    auto recvCallback = [&](const char *recvData, size_t recvSize) {
        if (localCallCount == 0)
        {
            EXPECT_EQ(recvSize, size1);
            EXPECT_STREQ(recvData, data1);
        }
        else if (localCallCount == 1)
        {
            EXPECT_EQ(recvSize, size2);
            EXPECT_STREQ(recvData, data2);
        }
        else
        {
            FAIL() << "Callback called more than twice";
        }
        localCallCount++;
        callbackCount++;
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // Deliver messages 0 and 1 in order
    serverChannel->processReceivedData(0, std::make_shared<std::vector<char>>(data1, data1 + size1), 0);
    serverChannel->processReceivedData(1, std::make_shared<std::vector<char>>(data2, data2 + size2), 0);
    // Now nextMessageId==2; sending messageId=0 again should be silently dropped
    serverChannel->processReceivedData(0, std::make_shared<std::vector<char>>(dataOld, dataOld + sizeOld), 0);

    // Wait for async delivery thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_EQ(callbackCount.load(), 2);
}

// Test that the reorder timeout skips missing messages when a later packet triggers the check
TEST_F(TcpVirtualChannelTest, ReorderTimeoutTest)
{
    std::atomic<int> callbackCount = 0;
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;

    const char *data2 = "message id1";
    size_t size2 = strlen(data2) + 1;

    const char *data3 = "message id2";
    size_t size3 = strlen(data3) + 1;

    int localCallCount = 0;
    auto recvCallback = [&](const char *recvData, size_t recvSize) {
        if (localCallCount == 0)
        {
            // After timeout, messageId=1 is delivered first
            EXPECT_EQ(recvSize, size2);
            EXPECT_STREQ(recvData, data2);
        }
        else if (localCallCount == 1)
        {
            // Then messageId=2
            EXPECT_EQ(recvSize, size3);
            EXPECT_STREQ(recvData, data3);
        }
        else
        {
            FAIL() << "Callback called more than twice";
        }
        localCallCount++;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbackCount++;
        }
        callbackCondition.notify_all();
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // Send messageId=1 (skipping 0) — gets buffered, gap timer starts
    serverChannel->processReceivedData(1, std::make_shared<std::vector<char>>(data2, data2 + size2), 0);

    // Nothing should be delivered yet (waiting for messageId=0)
    ASSERT_EQ(callbackCount.load(), 0);

    // Wait longer than the 500ms reorder timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Still 0 — passive timeout only triggers on the next processReceivedData call
    ASSERT_EQ(callbackCount.load(), 0);

    // Send messageId=2 — this triggers the timeout check, which skips id=0,
    // then drains id=1 and id=2
    serverChannel->processReceivedData(2, std::make_shared<std::vector<char>>(data3, data3 + size3), 0);

    // Wait for async delivery thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Both messages should now be delivered
    ASSERT_EQ(callbackCount.load(), 2);
}

// Test that a second gap starts a fresh timer after the first gap is resolved
TEST_F(TcpVirtualChannelTest, ReorderTimeoutResetsAfterGapFilled)
{
    std::atomic<int> callbackCount = 0;

    const char *data0 = "msg0";
    size_t size0 = strlen(data0) + 1;
    const char *data1 = "msg1";
    size_t size1 = strlen(data1) + 1;
    const char *data3 = "msg3";
    size_t size3 = strlen(data3) + 1;
    const char *data4 = "msg4";
    size_t size4 = strlen(data4) + 1;

    auto recvCallback = [&](const char * /*recvData*/, size_t /*recvSize*/) {
        callbackCount++;
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // Create first gap: send id=1 (missing id=0)
    serverChannel->processReceivedData(1, std::make_shared<std::vector<char>>(data1, data1 + size1), 0);
    ASSERT_EQ(callbackCount.load(), 0);

    // Fill the gap quickly — both 0 and 1 delivered, timer clears
    serverChannel->processReceivedData(0, std::make_shared<std::vector<char>>(data0, data0 + size0), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(callbackCount.load(), 2);

    // Create second gap: send id=3 (missing id=2), fresh timer starts now
    serverChannel->processReceivedData(3, std::make_shared<std::vector<char>>(data3, data3 + size3), 0);
    ASSERT_EQ(callbackCount.load(), 2);

    // Wait past the timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Trigger timeout check with id=4
    serverChannel->processReceivedData(4, std::make_shared<std::vector<char>>(data4, data4 + size4), 0);

    // Wait for async delivery thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // id=3 and id=4 delivered after skipping id=2
    ASSERT_EQ(callbackCount.load(), 4);
}
