#include "Socket.h"
#include "TcpVirtualChannel.h"
#include <condition_variable>
#include <gtest/gtest.h>

// Test fixture for TcpVirtualChannel
class TcpVirtualChannelTest : public ::testing::Test
{
  protected:
    TcpVirtualChannel *clientChannel;
    TcpVirtualChannel *serverChannel;
    SocketFd serverSocket;
    SocketFd clientSocket;
    SocketFd serverAcceptedSocket;
    const char *serverAddrStr = "127.0.0.1";
    int serverPort = 10000;
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
        std::cout << "Setting up server socket..." << std::endl;
        serverSocket = CreateSocket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(serverPort);
        auto result = SocketBind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
        ASSERT_EQ(result, 0);

        std::cout << "Server listening on port " << serverPort << "..." << std::endl;
        SocketListen(serverSocket, 1);

        // Notify that server is ready to accept connections
        {
            std::unique_lock<std::mutex> lock(serverMutex);
            serverReady = true;
            serverCondition.notify_all();
        }

        // Accept a connection
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        std::cout << "Server waiting to accept connection..." << std::endl;
        serverAcceptedSocket = SocketAccept(serverSocket, (sockaddr *)&clientAddr, &clientAddrLen);
        serverChannel = new TcpVirtualChannel({serverAcceptedSocket});

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

        clientSocket = CreateSocket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(serverAddrStr);
        serverAddr.sin_port = htons(serverPort);
        std::cout << "Client connecting to server at " << serverAddrStr << ":" << serverPort << "..." << std::endl;
        auto result = SocketConnect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
        ASSERT_EQ(result, 0);
        std::cout << "Client connected to server socket." << std::endl;

        clientChannel = new TcpVirtualChannel({clientSocket});

        std::cout << "Client connected to server." << std::endl;
    }

    void TearDown() override
    {

        if (clientChannel->isOpen())
        {
            clientChannel->close();
            delete clientChannel;
        }
        if (serverChannel->isOpen())
        {
            serverChannel->close();
            delete serverChannel;
        }

#ifdef _WIN32
        WSACleanup();
#endif
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
        callbackCondition.wait(lock, [&callbackCalled]() { return callbackCalled.load(); });
    });

    // client sends data
    std::thread clientThread([this, data, size]() { clientChannel->send(data, size); });

    clientThread.join();
    serverThread.join();
}

// Send and Receive multiple times
TEST_F(TcpVirtualChannelTest, SendRecvTest2)
{
    std::atomic<bool> callbackCalled = false;
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;

    const char *data = "test data";
    size_t size = strlen(data) + 1;

    const char *data2 = "test data 2";
    size_t size2 = strlen(data2) + 1;

    static int callCount = 0;

    // recv data callback
    auto recvCallback = [&callbackCalled, &callbackMutex, &callbackCondition, this, data, size, data2,
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
            callbackCalled = true;
        }
        callbackCondition.notify_one();
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // server thread to wait for callback
    std::thread serverThread([&callbackCalled, &callbackMutex, &callbackCondition]() {
        {
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&callbackCalled]() { return callbackCalled.load(); });
            callbackCalled = false;
        }
        {
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&callbackCalled]() { return callbackCalled.load(); });
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

// Check if we can handle out-of-order messages
TEST_F(TcpVirtualChannelTest, processReceivedDataTest)
{

    std::atomic<bool> callbackCalled = false;
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
    auto recvCallback = [&callbackCalled, &callbackMutex, &callbackCondition, this, data1, size1, data2, size2, data3,
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
            FAIL() << "Callback called more than three times";
        }
        callCount++;
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
        for (int i = 0; i < 3; i++)
        {
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&callbackCalled]() { return callbackCalled.load(); });
            callbackCalled = false;
        }
    });

    // Simulate out-of-order message reception
    std::thread clientThread([this, data1, size1, data2, size2, data3, size3]() {
            // instead of sending via channel, directly call processReceivedData to simulate out-of-order
        log_info("Simulating out-of-order message reception");
        serverChannel->processReceivedData(2, std::make_shared<std::vector<char>>(data3, data3 + size3));
        serverChannel->processReceivedData(0, std::make_shared<std::vector<char>>(data1, data1 + size1));
        serverChannel->processReceivedData(1, std::make_shared<std::vector<char>>(data2, data2 + size2));
    });

    clientThread.join();
    serverThread.join();
    ASSERT_EQ(callCount, 3);
}
