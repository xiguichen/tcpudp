#include <gtest/gtest.h>
#include "Socket.h"
#include "TcpVirtualChannel.h"
#include <condition_variable>

// Test fixture for TcpVirtualChannel
class TcpVirtualChannelTest : public ::testing::Test {
protected:
    TcpVirtualChannel* clientChannel;
    TcpVirtualChannel* serverChannel;
    SocketFd serverSocket;
    SocketFd clientSocket;
    SocketFd serverAcceptedSocket;
    const char* serverAddrStr = "127.0.0.1";
    int serverPort = 10000;;
    std::mutex serverMutex;
    std::condition_variable serverCondition;
    bool serverReady = false;

    void SetUp() override {
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
        auto result = SocketBind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
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
        serverAcceptedSocket = SocketAccept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen);
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
        auto result = SocketConnect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
        ASSERT_EQ(result, 0);
        std::cout << "Client connected to server socket." << std::endl;

        clientChannel = new TcpVirtualChannel({clientSocket});

        std::cout << "Client connected to server." << std::endl;
    }


    void TearDown() override {

        if (clientChannel->isOpen()) {
            clientChannel->close();
            delete clientChannel;
        }
        if (serverChannel->isOpen()) {
            serverChannel->close();
            delete serverChannel;
        }

    }
};

// Test send method
TEST_F(TcpVirtualChannelTest, SendTest) {
    const char* data = "test data";
    size_t size = strlen(data);


    // recv data callback
    std::atomic<bool> callbackCalled = false;
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;

    auto recvCallback = [&callbackCalled, &callbackMutex, &callbackCondition, this, data, size](const char* recvData, size_t recvSize) {
        EXPECT_STREQ(recvData, data);
        EXPECT_EQ(recvSize, size);
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
    std::thread clientThread([this, data, size]() {
        clientChannel->send(data, size);
    });

    clientThread.join();
    serverThread.join();
}

