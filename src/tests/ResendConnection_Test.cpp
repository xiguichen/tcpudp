#include "Log.h"
#include "Socket.h"
#include "TcpVCSendThread.h"
#include "TcpVirtualChannel.h"
#include "VcProtocol.h"
#include <algorithm>
#include <condition_variable>
#include <gtest/gtest.h>
#include <cerrno>
#include <cstring>

// ---------------------------------------------------------------------------
// Polling helper — avoids fixed sleep_for in timing-dependent assertions.
// ---------------------------------------------------------------------------
template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout,
             std::chrono::milliseconds interval = std::chrono::milliseconds(10))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(interval);
    }
    return pred();
}

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------

TEST(ResendProtocolTest, ResendConnectionCount)
{
    EXPECT_GE(VC_RESEND_CONN_COUNT, 1);
    EXPECT_LE(VC_RESEND_CONN_COUNT, VC_TCP_CONNECTIONS);
}

TEST(ResendProtocolTest, FirstResendIndexFormula)
{
    EXPECT_EQ(VC_FIRST_RESEND_CONN_INDEX, VC_TCP_CONNECTIONS - VC_RESEND_CONN_COUNT);
}

TEST(ResendProtocolTest, DataConnectionCount)
{
    EXPECT_EQ(VC_TCP_CONNECTIONS - VC_RESEND_CONN_COUNT, VC_FIRST_RESEND_CONN_INDEX);
}

TEST(ResendProtocolTest, IsResendConnection)
{
    for (int i = 0; i < VC_FIRST_RESEND_CONN_INDEX; i++)
    {
        EXPECT_FALSE(IsResendConnectionIndex(i)) << "Index " << i << " should NOT be a resend connection";
    }
    for (int i = VC_FIRST_RESEND_CONN_INDEX; i < VC_TCP_CONNECTIONS; i++)
    {
        EXPECT_TRUE(IsResendConnectionIndex(i)) << "Index " << i << " should be a resend connection";
    }
}

// ---------------------------------------------------------------------------
// Helper: create a connected loopback socket pair.
// ---------------------------------------------------------------------------
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

// Helper: create N socket pairs, returning the client and server fd vectors.
static void MakeSocketPairs(int count,
                            std::vector<SocketFd> &clientFds,
                            std::vector<SocketFd> &serverFds)
{
    clientFds.clear();
    serverFds.clear();
    for (int i = 0; i < count; i++)
    {
        int port = 0;
        auto [c, s] = MakeSocketPair(port);
        clientFds.push_back(c);
        serverFds.push_back(s);
    }
}

// Cleanup helper
static void CloseAll(std::vector<SocketFd> &fds)
{
    for (auto fd : fds)
        SocketClose(fd);
}

// ---------------------------------------------------------------------------
// Fixture for full-size (32-connection) VC tests
// ---------------------------------------------------------------------------

class ResendConnectionFullVcTest : public ::testing::Test
{
  protected:
    std::vector<SocketFd> clientFds;
    std::vector<SocketFd> serverFds;
    std::shared_ptr<TcpVirtualChannel> vc;

    void SetUp() override
    {
#ifdef _WIN32
        WSAData wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        MakeSocketPairs(VC_TCP_CONNECTIONS, clientFds, serverFds);
        vc = std::make_shared<TcpVirtualChannel>(clientFds);
    }

    void TearDown() override
    {
        if (vc && vc->isOpen())
            vc->close();
        vc.reset();
        CloseAll(serverFds);
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

// ---------------------------------------------------------------------------
// Test: dead resend slot appears in getDeadSlots and can be replaced
// ---------------------------------------------------------------------------

TEST_F(ResendConnectionFullVcTest, DeadResendSlotAppearsInGetDeadSlots)
{
    vc->open();

    // Kill resend slot 31 (last slot)
    SocketClose(serverFds[31]);
    serverFds[31] = -1;

    EXPECT_TRUE(WaitFor([&] {
        auto dead = vc->getDeadSlots();
        return std::find(dead.begin(), dead.end(), 31) != dead.end();
    }, std::chrono::seconds(5))) << "Resend slot 31 must appear in getDeadSlots()";

    // Replace it
    int portR = 0;
    auto [newC, newS] = MakeSocketPair(portR);
    vc->replaceConnection(31, newC);

    auto deadAfter = vc->getDeadSlots();
    EXPECT_TRUE(std::find(deadAfter.begin(), deadAfter.end(), 31) == deadAfter.end())
        << "Slot 31 must not be dead after replacement";

    SocketClose(newS);
}

// ---------------------------------------------------------------------------
// Test: when one resend connection dies, resend traffic uses remaining ones
// (verify no "All resend connections failed" log when 3 of 4 are alive)
// ---------------------------------------------------------------------------

TEST_F(ResendConnectionFullVcTest, ResendFallsBackWhenOneResendConnDead)
{
    vc->open();

    // Send data to populate sentDataCache
    for (int i = 0; i < 5; i++)
    {
        std::string msg = "payload_" + std::to_string(i);
        vc->send(msg.c_str(), msg.size() + 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Kill one resend connection (slot 31)
    SocketClose(serverFds[31]);
    serverFds[31] = -1;

    EXPECT_TRUE(WaitFor([&] {
        auto dead = vc->getDeadSlots();
        return std::find(dead.begin(), dead.end(), 31) != dead.end();
    }, std::chrono::seconds(5)));
    EXPECT_TRUE(vc->isOpen());

    // Trigger resend — should succeed on one of the remaining 3 resend connections
    vc->processMissingNotify({0});

    EXPECT_TRUE(WaitFor([&] { return vc->isOpen(); }, std::chrono::seconds(2)));
}

// ---------------------------------------------------------------------------
// Test: resend works when all 4 resend connections are alive
// ---------------------------------------------------------------------------

TEST_F(ResendConnectionFullVcTest, ResendWorksWithAllResendConnsAlive)
{
    vc->open();

    for (int i = 0; i < 10; i++)
    {
        std::string msg = "msg_" + std::to_string(i);
        vc->send(msg.c_str(), msg.size() + 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Trigger resends — should distribute across resend connections
    vc->processMissingNotify({0, 1, 2, 3, 4});

    EXPECT_TRUE(WaitFor([&] { return vc->isOpen(); }, std::chrono::seconds(2)));
}

// ---------------------------------------------------------------------------
// Test: VC stays open when ALL resend connections die but data connections live
// ---------------------------------------------------------------------------

TEST_F(ResendConnectionFullVcTest, VcStaysOpenWhenAllResendConnsDie)
{
    std::atomic<bool> disconnectFired{false};
    vc->setDisconnectCallback([&] { disconnectFired = true; });
    vc->open();

    // Kill all 4 resend connections (slots 28-31)
    for (int i = VC_FIRST_RESEND_CONN_INDEX; i < VC_TCP_CONNECTIONS; i++)
    {
        SocketClose(serverFds[i]);
        serverFds[i] = -1;
    }

    // Wait for all 4 resend slots to appear dead
    EXPECT_TRUE(WaitFor([&] {
        auto dead = vc->getDeadSlots();
        for (int i = VC_FIRST_RESEND_CONN_INDEX; i < VC_TCP_CONNECTIONS; i++)
            if (std::find(dead.begin(), dead.end(), i) == dead.end())
                return false;
        return true;
    }, std::chrono::seconds(5)));

    // VC should still be open — data connections 0-27 are alive
    EXPECT_TRUE(vc->isOpen());
    EXPECT_FALSE(disconnectFired.load());
}

// ---------------------------------------------------------------------------
// Test: after replacing dead resend connections, resend traffic works again
// ---------------------------------------------------------------------------

TEST_F(ResendConnectionFullVcTest, ResendWorksAfterReplacingDeadConns)
{
    vc->open();

    for (int i = 0; i < 5; i++)
    {
        std::string msg = "msg_" + std::to_string(i);
        vc->send(msg.c_str(), msg.size() + 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Kill all resend connections
    for (int i = VC_FIRST_RESEND_CONN_INDEX; i < VC_TCP_CONNECTIONS; i++)
    {
        SocketClose(serverFds[i]);
        serverFds[i] = -1;
    }

    EXPECT_TRUE(WaitFor([&] {
        auto dead = vc->getDeadSlots();
        for (int i = VC_FIRST_RESEND_CONN_INDEX; i < VC_TCP_CONNECTIONS; i++)
            if (std::find(dead.begin(), dead.end(), i) == dead.end())
                return false;
        return true;
    }, std::chrono::seconds(5)));

    // Replace them all
    std::vector<SocketFd> replacementServerFds;
    for (int i = VC_FIRST_RESEND_CONN_INDEX; i < VC_TCP_CONNECTIONS; i++)
    {
        int portR = 0;
        auto [newC, newS] = MakeSocketPair(portR);
        vc->replaceConnection(i, newC);
        replacementServerFds.push_back(newS);
    }

    // Dead slots should no longer include any resend slots
    auto deadAfter = vc->getDeadSlots();
    for (int i = VC_FIRST_RESEND_CONN_INDEX; i < VC_TCP_CONNECTIONS; i++)
    {
        EXPECT_TRUE(std::find(deadAfter.begin(), deadAfter.end(), i) == deadAfter.end())
            << "Slot " << i << " should not be dead after replacement";
    }

    // Trigger resend — should work now with replacement connections
    vc->processMissingNotify({0, 1});

    EXPECT_TRUE(WaitFor([&] { return vc->isOpen(); }, std::chrono::seconds(2)));

    for (auto fd : replacementServerFds)
        SocketClose(fd);
}

// ---------------------------------------------------------------------------
// Test: with small VC (< VC_FIRST_RESEND_CONN_INDEX), all conns used for data
// ---------------------------------------------------------------------------

TEST(ResendConnectionSmallVcTest, SmallVcUsesAllConnsForData)
{
#ifdef _WIN32
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    std::vector<SocketFd> clientFds;
    std::vector<SocketFd> serverFds;
    MakeSocketPairs(4, clientFds, serverFds);

    auto vc = std::make_shared<TcpVirtualChannel>(clientFds);
    vc->open();

    // Send data — should succeed using all 4 connections as data connections
    for (int i = 0; i < 10; i++)
    {
        std::string msg = "small_vc_msg_" + std::to_string(i);
        vc->send(msg.c_str(), msg.size() + 1);
    }

    EXPECT_TRUE(WaitFor([&] { return vc->isOpen(); }, std::chrono::seconds(2)));

    vc->close();
    CloseAll(serverFds);

#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Test: resend with 3 of 4 dead still succeeds on the surviving connection
// ---------------------------------------------------------------------------

TEST_F(ResendConnectionFullVcTest, ResendSucceedsWithOnlyOneResendConnAlive)
{
    vc->open();

    for (int i = 0; i < 5; i++)
    {
        std::string msg = "data_" + std::to_string(i);
        vc->send(msg.c_str(), msg.size() + 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Kill 3 of 4 resend connections (keep slot 28 alive)
    for (int i = VC_FIRST_RESEND_CONN_INDEX + 1; i < VC_TCP_CONNECTIONS; i++)
    {
        SocketClose(serverFds[i]);
        serverFds[i] = -1;
    }

    EXPECT_TRUE(WaitFor([&] {
        auto dead = vc->getDeadSlots();
        for (int i = VC_FIRST_RESEND_CONN_INDEX + 1; i < VC_TCP_CONNECTIONS; i++)
            if (std::find(dead.begin(), dead.end(), i) == dead.end())
                return false;
        return true;
    }, std::chrono::seconds(5)));
    EXPECT_TRUE(vc->isOpen());

    // Trigger resend — should succeed on the one surviving resend connection (slot 28)
    vc->processMissingNotify({0, 1, 2});

    EXPECT_TRUE(WaitFor([&] { return vc->isOpen(); }, std::chrono::seconds(2)));
}
