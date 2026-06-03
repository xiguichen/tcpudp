#include "Socket.h"
#include <gtest/gtest.h>

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
