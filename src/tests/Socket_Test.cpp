#include "Socket.h"
#include <gtest/gtest.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>
#include <utility>

// Returns the current SO_KEEPALIVE flag for a socket (0 = off, 1 = on), or -1 on error.
static int GetKeepAliveFlag(SocketFd fd)
{
    int value = 0;
#ifdef _WIN32
    int len = sizeof(value);
    if (getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&value, &len) != 0)
        return -1;
#else
    socklen_t len = sizeof(value);
    if (getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &value, &len) != 0)
        return -1;
#endif
    return value ? 1 : 0;
}

class SocketKeepAliveTest : public ::testing::Test
{
  protected:
    void SetUp() override { SocketInit(); }
    void TearDown() override { SocketClearnup(); }
};

TEST_F(SocketKeepAliveTest, EnableTurnsOnKeepAlive)
{
    SocketFd fd = SocketCreate(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(fd, (SocketFd)-1);
    EXPECT_EQ(GetKeepAliveFlag(fd), 0) << "freshly created socket should default to keepalive off";

    int rc = SocketSetKeepAlive(fd, true, 10, 5, 3);

    EXPECT_EQ(rc, 0) << "SocketSetKeepAlive should report success";
    EXPECT_EQ(GetKeepAliveFlag(fd), 1) << "SO_KEEPALIVE must be enabled after the call";

    SocketClose(fd);
}

// ---------------------------------------------------------------------------
// Direct (non-blocking) send/recv error-sentinel mapping.
//
// Regression guard: SOCKET_ERROR_WOULD_BLOCK == -1, and recv()/send() also
// return a raw -1 on a hard error. RecvTcpDirect/SendTcpDirect must map a
// genuine connection failure (peer FIN or RST) to SOCKET_ERROR_CLOSED (-3)
// rather than returning the raw -1, which would alias WOULD_BLOCK and leave
// the broken connection undetected (an idle "alive" zombie) — the failure
// mode behind the reconnect storm under load.
// ---------------------------------------------------------------------------

namespace
{

std::pair<SocketFd, SocketFd> MakeConnectedPair()
{
    SocketFd listenFd = SocketCreate(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    SocketBind(listenFd, (sockaddr *)&addr, sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(listenFd, (sockaddr *)&addr, &len);
    SocketListen(listenFd, 1);

    SocketFd a = SocketCreate(AF_INET, SOCK_STREAM, 0);
    SocketConnect(a, (sockaddr *)&addr, sizeof(addr));
    sockaddr_in accAddr{};
    socklen_t accLen = sizeof(accAddr);
    SocketFd b = SocketAccept(listenFd, (sockaddr *)&accAddr, &accLen);
    SocketClose(listenFd);
    return {a, b};
}

template <typename Pred>
bool PollUntil(Pred pred, std::chrono::milliseconds timeout = std::chrono::seconds(3))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

// SO_LINGER with l_linger==0 makes close() emit a RST instead of a FIN.
void ForceReset(SocketFd fd)
{
    struct linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
#ifdef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_LINGER, (const char *)&lg, sizeof(lg));
#else
    setsockopt(fd, SOL_SOCKET, SO_LINGER, (const char *)&lg, sizeof(lg));
#endif
    SocketClose(fd);
}

} // namespace

class SocketDirectErrorTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        SocketInit();
#ifndef _WIN32
        // Sending to a reset peer raises SIGPIPE on POSIX; ignore it so the
        // send path returns an error code we can assert on instead of aborting.
        std::signal(SIGPIPE, SIG_IGN);
#endif
    }
    void TearDown() override { SocketClearnup(); }
};

// Clean peer close (FIN -> recv()==0) maps to SOCKET_ERROR_CLOSED.
TEST_F(SocketDirectErrorTest, RecvDirectReturnsClosedOnCleanPeerClose)
{
    auto pair = MakeConnectedPair();
    SocketFd a = pair.first, b = pair.second;
    SetSocketNonBlocking(a);

    SocketShutdown(b, SHUT_RDWR);
    SocketClose(b);

    char buf[64];
    ssize_t rc = SOCKET_ERROR_WOULD_BLOCK;
    ASSERT_TRUE(PollUntil([&] {
        rc = RecvTcpDirect(a, buf, sizeof(buf), 0);
        return rc != SOCKET_ERROR_WOULD_BLOCK;
    })) << "recv never observed the peer close";
    EXPECT_EQ(rc, SOCKET_ERROR_CLOSED);

    SocketClose(a);
}

// Peer RST maps to SOCKET_ERROR_CLOSED, NOT raw -1 (== WOULD_BLOCK).
TEST_F(SocketDirectErrorTest, RecvDirectReturnsClosedOnPeerReset)
{
    auto pair = MakeConnectedPair();
    SocketFd a = pair.first, b = pair.second;
    SetSocketNonBlocking(a);

    ForceReset(b);

    // Nudge the local stack so it processes the RST (some platforms only surface
    // ECONNRESET after an I/O attempt).
    const char ping[] = "x";
    SendTcpDirect(a, ping, sizeof(ping), 0);

    char buf[64];
    ssize_t rc = SOCKET_ERROR_WOULD_BLOCK;
    ASSERT_TRUE(PollUntil([&] {
        rc = RecvTcpDirect(a, buf, sizeof(buf), 0);
        return rc != SOCKET_ERROR_WOULD_BLOCK;
    })) << "recv never observed the reset";
    EXPECT_EQ(rc, SOCKET_ERROR_CLOSED)
        << "a reset peer must be reported as CLOSED, not aliased to WOULD_BLOCK";

    SocketClose(a);
}

// Send to a reset peer eventually reports SOCKET_ERROR_CLOSED (not raw -1),
// so the send thread can reap the dead connection.
TEST_F(SocketDirectErrorTest, SendDirectReturnsClosedOnPeerReset)
{
    auto pair = MakeConnectedPair();
    SocketFd a = pair.first, b = pair.second;
    SetSocketNonBlocking(a);

    ForceReset(b);

    char payload[256];
    std::memset(payload, 'z', sizeof(payload));
    ssize_t rc = SOCKET_ERROR_WOULD_BLOCK;
    ASSERT_TRUE(PollUntil([&] {
        rc = SendTcpDirect(a, payload, sizeof(payload), 0);
        return rc == SOCKET_ERROR_CLOSED;
    })) << "send never reported the reset as CLOSED";
    EXPECT_EQ(rc, SOCKET_ERROR_CLOSED);

    SocketClose(a);
}
