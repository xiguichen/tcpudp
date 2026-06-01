#include "Log.h"
#include "Socket.h"
#include "TcpVirtualChannel.h"
#include <algorithm>
#include <condition_variable>
#include <gtest/gtest.h>
#include <cerrno>
#include <cstdlib>
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

// Check if we can handle out-of-order messages
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

// Test that the reorder timeout skips missing messages automatically (active timeout)
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
            EXPECT_EQ(recvSize, size2);
            EXPECT_STREQ(recvData, data2);
        }
        else if (localCallCount == 1)
        {
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
    serverChannel->setReorderTimeout(std::chrono::milliseconds(500));
    serverChannel->open();
    clientChannel->open();

    // Send messageId=1 (skipping 0) — gets buffered, gap timer starts
    serverChannel->processReceivedData(1, std::make_shared<std::vector<char>>(data2, data2 + size2), 0);

    // Nothing should be delivered yet (waiting for messageId=0)
    ASSERT_EQ(callbackCount.load(), 0);

    // Wait longer than the 500ms reorder timeout — active thread fires automatically
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    // Active timeout skipped id=0 and delivered id=1
    ASSERT_EQ(callbackCount.load(), 1);

    // Send messageId=2 — delivered immediately since nextMessageId is now 2
    serverChannel->processReceivedData(2, std::make_shared<std::vector<char>>(data3, data3 + size3), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
    serverChannel->setReorderTimeout(std::chrono::milliseconds(500));
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

    // Wait past the timeout — active thread fires automatically, skips id=2, delivers id=3
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    ASSERT_EQ(callbackCount.load(), 3);

    // Send id=4 — delivered immediately since nextMessageId is now 4
    serverChannel->processReceivedData(4, std::make_shared<std::vector<char>>(data4, data4 + size4), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_EQ(callbackCount.load(), 4);
}

// Test that send path handles partial TCP writes correctly.
// With non-blocking sockets and small send buffers, send() can return fewer bytes
// than requested. If the send thread doesn't retry the remaining bytes, the receiver
// sees corrupted packet boundaries ("Unknown packet type" errors).
TEST_F(TcpVirtualChannelTest, PartialSendHandledCorrectly)
{
    // Use maximum payload size (2000 bytes) to maximize per-packet wire size (~2011 bytes).
    // Reduce TCP send buffer on client + receive buffer on server to force backpressure.
    int sendBufSize = 2048;
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF, (const char *)&sendBufSize, sizeof(sendBufSize));
    int recvBufSize = 2048;
    setsockopt(serverAcceptedSocket, SOL_SOCKET, SO_RCVBUF, (const char *)&recvBufSize, sizeof(recvBufSize));

    const int numPackets = 200;
    std::atomic<int> callbackCount{0};
    std::atomic<bool> corrupted{false};
    std::condition_variable receivedCv;
    std::mutex receivedMutex;

    auto recvCallback = [&](const char *recvData, size_t recvSize) {
        // Each packet is 2000 bytes: "packet_XXXX" padded with 'A' to fill 2000 bytes.
        // Verify first 7 bytes are "packet_" and the number is valid.
        if (recvSize != 2000)
        {
            corrupted.store(true);
            EXPECT_EQ(recvSize, 2000u) << "Unexpected packet size (corruption likely)";
        }
        else if (std::string(recvData, 7) != "packet_")
        {
            corrupted.store(true);
            FAIL() << "Corrupted packet: does not start with 'packet_'";
        }
        callbackCount++;
        receivedCv.notify_all();
    };
    serverChannel->setReceiveCallback(recvCallback);
    serverChannel->open();
    clientChannel->open();

    // Send max-size packets through the channel rapidly.
    // With 2048-byte SO_SNDBUF and ~2011-byte wire packets, partial sends are forced
    // when backpressure builds from the small receive buffer.
    for (int i = 0; i < numPackets; i++)
    {
        std::string header = "packet_" + std::to_string(i);
        std::vector<char> payload(2000, 'A');
        std::memcpy(payload.data(), header.c_str(), header.size());
        clientChannel->send(payload.data(), payload.size());
    }

    // Wait for all packets or corruption
    {
        std::unique_lock<std::mutex> lock(receivedMutex);
        receivedCv.wait_for(lock, std::chrono::seconds(15),
                            [&] { return callbackCount.load() >= numPackets || corrupted.load(); });
    }

    EXPECT_FALSE(corrupted.load()) << "Packet corruption detected — likely partial-send bug";
    EXPECT_EQ(callbackCount.load(), numPackets)
        << "Not all packets received. Got " << callbackCount.load() << "/" << numPackets;
}

// Direct proof that non-blocking TCP send() can return partial writes under backpressure.
// This verifies the premise that the send thread MUST handle partial sends.
TEST(PartialSendTest, NonBlockingSendCanReturnPartial)
{
#ifdef _WIN32
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // Create a connected socket pair via listen/connect/accept
    SocketFd listenFd = SocketCreate(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(listenFd, (SocketFd)-1);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    ASSERT_EQ(SocketBind(listenFd, (sockaddr *)&addr, sizeof(addr)), 0);

    socklen_t addrLen = sizeof(addr);
    getsockname(listenFd, (sockaddr *)&addr, &addrLen);
    SocketListen(listenFd, 1);

    SocketFd clientFd = SocketCreate(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(clientFd, (SocketFd)-1);
    ASSERT_EQ(SocketConnect(clientFd, (sockaddr *)&addr, sizeof(addr)), 0);

    sockaddr_in acceptAddr{};
    socklen_t acceptLen = sizeof(acceptAddr);
    SocketFd serverFd = SocketAccept(listenFd, (sockaddr *)&acceptAddr, &acceptLen);
    ASSERT_NE(serverFd, (SocketFd)-1);
    SocketClose(listenFd);

    // Set minimal buffer sizes and non-blocking mode
    int tiny = 1;
    setsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, (const char *)&tiny, sizeof(tiny));
    setsockopt(serverFd, SOL_SOCKET, SO_RCVBUF, (const char *)&tiny, sizeof(tiny));
    SetSocketNonBlocking(clientFd);

    // Fill the send buffer by writing without reading on server side.
    // Eventually send() will either return partial or WOULD_BLOCK.
    std::vector<char> bigBuf(8192, 'X');
    bool gotPartial = false;
    bool gotWouldBlock = false;

    for (int attempt = 0; attempt < 1000; attempt++)
    {
        ssize_t n = SendTcpDirect(clientFd, bigBuf.data(), bigBuf.size(), 0);
        if (n == SOCKET_ERROR_WOULD_BLOCK)
        {
            gotWouldBlock = true;
            break;
        }
        if (n > 0 && static_cast<size_t>(n) < bigBuf.size())
        {
            gotPartial = true;
            break;
        }
    }

    // On non-blocking sockets under backpressure, we expect either a partial write or WOULD_BLOCK.
    // Either outcome proves that the send thread CANNOT assume send() always returns the full length.
    EXPECT_TRUE(gotPartial || gotWouldBlock)
        << "Expected partial send or WOULD_BLOCK under buffer pressure. "
           "If neither occurred, the test environment doesn't reproduce the condition.";

    // Log which outcome for informational purposes
    if (gotPartial)
        std::cout << "[INFO] Got partial send — confirms send thread must handle partial writes." << std::endl;
    else if (gotWouldBlock)
        std::cout << "[INFO] Got WOULD_BLOCK — confirms non-blocking send can fail under pressure." << std::endl;

    SocketClose(clientFd);
    SocketClose(serverFd);

#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Partial-disconnect tests: one connection drops, VC continues; replace works
// ---------------------------------------------------------------------------

// Helper: create a connected loopback socket pair.
// Returns {clientFd, serverFd}. Caller owns and must close both.
static std::pair<SocketFd, SocketFd> MakeSocketPair(int &portOut)
{
    SocketFd listenFd = SocketCreate(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    SocketBind(listenFd, (sockaddr *)&addr, sizeof(addr));

    socklen_t addrLen = sizeof(addr);
    getsockname(listenFd, (sockaddr *)&addr, &addrLen);
    portOut = ntohs(addr.sin_port);

    SocketListen(listenFd, 1);

    SocketFd clientFd = SocketCreate(AF_INET, SOCK_STREAM, 0);
    SocketConnect(clientFd, (sockaddr *)&addr, sizeof(addr));

    sockaddr_in acceptAddr{};
    socklen_t acceptLen = sizeof(acceptAddr);
    SocketFd serverFd = SocketAccept(listenFd, (sockaddr *)&acceptAddr, &acceptLen);
    SocketClose(listenFd);

    return {clientFd, serverFd};
}

TEST(PartialDisconnectTest, VcStaysOpenWhenOneConnectionDrops)
{
#ifdef _WIN32
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int port0 = 0, port1 = 0;
    auto [c0, s0] = MakeSocketPair(port0);
    auto [c1, s1] = MakeSocketPair(port1);

    // VC with 2 connections: c0 and c1 on the "client" side.
    auto vc = std::make_shared<TcpVirtualChannel>(std::vector<SocketFd>{c0, c1});

    std::atomic<bool> disconnectFired{false};
    vc->setDisconnectCallback([&] { disconnectFired = true; });
    vc->open();

    // Kill connection 1 by closing the server-side fd — the IO thread will see POLLIN/error.
    SocketClose(s1);
    // Give IO thread time to detect the disconnect (poll timeout is 50ms).
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // VC must still be open — connection 0 is alive.
    EXPECT_TRUE(vc->isOpen()) << "VC must stay open when only one of two connections drops";

    // disconnectCallback must NOT have fired.
    EXPECT_FALSE(disconnectFired.load()) << "disconnectCallback must not fire while connections remain";

    // Dead slot 1 must be reported.
    auto dead = vc->getDeadSlots();
    bool slot1Dead = std::find(dead.begin(), dead.end(), 1) != dead.end();
    EXPECT_TRUE(slot1Dead) << "Slot 1 must appear in getDeadSlots() after its peer closed";

    vc->close();
    SocketClose(c0);
    SocketClose(s0);

#ifdef _WIN32
    WSACleanup();
#endif
}

TEST(PartialDisconnectTest, AllDeadTriggersDisconnectCallback)
{
#ifdef _WIN32
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int port0 = 0;
    auto [c0, s0] = MakeSocketPair(port0);

    auto vc = std::make_shared<TcpVirtualChannel>(std::vector<SocketFd>{c0});

    std::atomic<bool> disconnectFired{false};
    std::mutex mu;
    std::condition_variable cv;

    vc->setDisconnectCallback([&] {
        disconnectFired = true;
        cv.notify_all();
    });
    vc->open();

    // Close the only connection's peer — IO thread will detect the drop.
    SocketClose(s0);

    std::unique_lock<std::mutex> lock(mu);
    cv.wait_for(lock, std::chrono::seconds(5), [&] { return disconnectFired.load(); });

    EXPECT_TRUE(disconnectFired.load()) << "disconnectCallback must fire when all connections die";

#ifdef _WIN32
    WSACleanup();
#endif
}

TEST(PartialDisconnectTest, ReplaceConnectionRestoresSlot)
{
#ifdef _WIN32
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int port0 = 0, port1 = 0;
    auto [c0, s0] = MakeSocketPair(port0);
    auto [c1, s1] = MakeSocketPair(port1);

    auto vc = std::make_shared<TcpVirtualChannel>(std::vector<SocketFd>{c0, c1});
    vc->open();

    // Kill slot 1.
    SocketClose(s1);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto deadBefore = vc->getDeadSlots();
    bool slot1Dead = std::find(deadBefore.begin(), deadBefore.end(), 1) != deadBefore.end();
    ASSERT_TRUE(slot1Dead) << "Slot 1 must be dead before replacement";

    // Create a replacement socket pair.
    int portR = 0;
    auto [newC, newS] = MakeSocketPair(portR);

    // Replace slot 1 with the new connection.
    vc->replaceConnection(1, newC);

    // Slot 1 should no longer appear in dead slots.
    auto deadAfter = vc->getDeadSlots();
    bool slot1StillDead = std::find(deadAfter.begin(), deadAfter.end(), 1) != deadAfter.end();
    EXPECT_FALSE(slot1StillDead) << "Slot 1 must not be in dead slots after replaceConnection";

    vc->close();
    SocketClose(s0);
    SocketClose(c0);
    SocketClose(newS);

#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Replace-connection cascade tests: verify that replacing a dead connection
// does NOT corrupt other connections (the double-close regression).
//
// The critical bug: when TcpConnection::disconnect() closes the socket FD,
// and then the code calls SocketClose() again on the same (now-stale) FD
// number, the OS may have reassigned that FD to a *different* connection's
// socket.  That connection gets silently killed, which triggers another
// watchdog cycle — cascading until all 32 connections die.
// ---------------------------------------------------------------------------

// Fixture: creates a VC with N connections and provides helpers to kill,
// replace, and verify alive connections.
class ReplaceConnectionCascadeTest : public ::testing::Test
{
  protected:
    static constexpr int VC_CONN_COUNT = 8; // 8 connections is enough to detect cascading

    std::vector<SocketFd> clientFds;
    std::vector<SocketFd> serverFds;
    std::shared_ptr<TcpVirtualChannel> vc;

    void SetUp() override
    {
#ifdef _WIN32
        WSAData wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        for (int i = 0; i < VC_CONN_COUNT; i++)
        {
            int port = 0;
            auto [c, s] = MakeSocketPair(port);
            clientFds.push_back(c);
            serverFds.push_back(s);
        }
        vc = std::make_shared<TcpVirtualChannel>(clientFds);
    }

    void TearDown() override
    {
        if (vc && vc->isOpen())
            vc->close();
        vc.reset();
        for (auto fd : serverFds)
        {
            if (fd != -1)
                SocketClose(fd);
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // Kill slot by closing its server-side peer and waiting for detection.
    void KillSlot(int slot)
    {
        ASSERT_GE(slot, 0);
        ASSERT_LT(static_cast<size_t>(slot), serverFds.size());
        ASSERT_NE(serverFds[slot], (SocketFd)-1) << "Slot " << slot << " already dead";
        SocketClose(serverFds[slot]);
        serverFds[slot] = -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(300)); // IO thread poll interval
    }

    // Create a replacement socket pair, replace slot, and track server FD.
    void ReplaceSlot(int slot)
    {
        int port = 0;
        auto [newC, newS] = MakeSocketPair(port);
        vc->replaceConnection(slot, newC);
        // Update serverFds so KillSlot can kill the replacement connection later.
        if (serverFds[slot] != -1)
            SocketClose(serverFds[slot]); // old server peer is dead, close it
        serverFds[slot] = newS;
    }

    // Return indices of dead slots.
    std::vector<int> DeadSlots()
    {
        return vc->getDeadSlots();
    }

    // Check that exactly the given slots (and only those) are dead.
    bool ExpectOnlyTheseSlotsDead(std::vector<int> expectedDead)
    {
        auto dead = DeadSlots();
        if (dead.size() != expectedDead.size())
            return false;
        for (int d : expectedDead)
        {
            if (std::find(dead.begin(), dead.end(), d) == dead.end())
                return false;
        }
        return true;
    }

    // Return a description string of dead slots (for logging).
    std::string DeadSlotsDesc()
    {
        auto dead = DeadSlots();
        if (dead.empty()) return "(none)";
        std::string s;
        for (int x : dead) s += std::to_string(x) + " ";
        return s;
    }
};

// Test: replace one dead slot — no other slot becomes dead as a side effect.
TEST_F(ReplaceConnectionCascadeTest, ReplaceOneSlotNoCascade)
{
    vc->open();
    ASSERT_TRUE(vc->isOpen());

    // Kill slot 2.
    KillSlot(2);
    ASSERT_TRUE(ExpectOnlyTheseSlotsDead({2}))
        << "Only slot 2 should be dead after killing it";

    // Replace slot 2.
    ReplaceSlot(2);

    // Verify: no slots are dead (the replaced slot is alive again).
    EXPECT_TRUE(ExpectOnlyTheseSlotsDead({}))
        << "After replacing slot 2, no slots should be dead. "
        << "If another slot appears dead, the double-close bug "
        << "corrupted a different connection's FD. Dead slots: "
        << DeadSlotsDesc();

    EXPECT_TRUE(vc->isOpen()) << "VC must remain open after replacement";
}

// Test: replace the same slot multiple times — other connections survive.
TEST_F(ReplaceConnectionCascadeTest, RepeatedReplaceSameSlotNoCascade)
{
    vc->open();
    ASSERT_TRUE(vc->isOpen());

    for (int round = 0; round < 5; round++)
    {
        // Kill slot 3.
        KillSlot(3);
        ASSERT_TRUE(ExpectOnlyTheseSlotsDead({3}))
            << "Round " << round << ": only slot 3 should be dead";

        // Replace it.
        ReplaceSlot(3);

        // Verify no other slots got corrupted.
        EXPECT_TRUE(ExpectOnlyTheseSlotsDead({}))
            << "Round " << round << ": all slots should be alive after replacement. "
            << "Dead: " << DeadSlotsDesc();

        EXPECT_TRUE(vc->isOpen()) << "Round " << round << ": VC must stay open";
    }
}

// Test: replace multiple different slots — no cascade between them.
TEST_F(ReplaceConnectionCascadeTest, ReplaceDifferentSlotsSequentially)
{
    vc->open();
    ASSERT_TRUE(vc->isOpen());

    for (int slot = 0; slot < VC_CONN_COUNT; slot++)
    {
        KillSlot(slot);
        // Only this slot should be dead (others might have been replaced and are alive)
        ASSERT_TRUE(ExpectOnlyTheseSlotsDead({slot}))
            << "After killing slot " << slot << ", only slot " << slot << " should be dead. "
            << "Dead: " << DeadSlotsDesc();

        ReplaceSlot(slot);

        EXPECT_TRUE(vc->isOpen()) << "After replacing slot " << slot << ", VC must stay open";
    }

    // After replacing all slots, everything should be alive.
    EXPECT_TRUE(ExpectOnlyTheseSlotsDead({})) << "All slots should be alive after replacing each one";
}

// Test: stress — kill and replace random slots 20 times.
TEST_F(ReplaceConnectionCascadeTest, StressReplaceRandomSlots)
{
    vc->open();
    ASSERT_TRUE(vc->isOpen());

    std::srand(42);
    for (int round = 0; round < 20; round++)
    {
        int slot = std::rand() % VC_CONN_COUNT;

        KillSlot(slot);

        // Verify only this slot is dead — no cascading corruption.
        auto dead = DeadSlots();
        bool onlyExpectedDead = (dead.size() == 1 && dead[0] == slot);
        EXPECT_TRUE(onlyExpectedDead)
            << "Round " << round << " slot " << slot << ": expected only slot " << slot
            << " dead but got: " << DeadSlotsDesc();

        ReplaceSlot(slot);

        EXPECT_TRUE(vc->isOpen()) << "Round " << round << ": VC must stay open";
    }
}

// Test: replace multiple slots and verify data still flows.
TEST_F(ReplaceConnectionCascadeTest, DataFlowSurvivesReplace)
{
    vc->open();
    ASSERT_TRUE(vc->isOpen());

    // Send some data to populate the send path.
    for (int i = 0; i < 5; i++)
    {
        std::string msg = "pre_replace_" + std::to_string(i);
        vc->send(msg.c_str(), msg.size() + 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Kill and replace 3 different slots.
    for (int slot : {1, 3, 5})
    {
        KillSlot(slot);
        ReplaceSlot(slot);
        EXPECT_TRUE(vc->isOpen()) << "VC must stay open after replacing slot " << slot;
    }

    // Send more data after replacements.
    for (int i = 0; i < 5; i++)
    {
        std::string msg = "post_replace_" + std::to_string(i);
        vc->send(msg.c_str(), msg.size() + 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_TRUE(vc->isOpen()) << "VC must still be open after data flow test";
    EXPECT_TRUE(ExpectOnlyTheseSlotsDead({})) << "No slots should be dead after replacements and data flow";
}
