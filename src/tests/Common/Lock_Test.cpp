#include "gtest/gtest.h"
#include "common/Lock.h"

class SocketLockTest : public ::testing::Test {
protected:
    SocketLock& socketLock = SocketLock::getInstance();

    void SetUp() override {
        // Clear the mutexes before each test
        socketLock.clearMutexes();
    }
};

TEST_F(SocketLockTest, GetLock) {
    int socket1 = 1;
    int socket2 = 2;

    // Acquire lock for socket1
    auto lock1 = socketLock.getLock(socket1);
    EXPECT_TRUE(lock1.owns_lock());

    // Acquire lock for socket2
    auto lock2 = socketLock.getLock(socket2);
    EXPECT_TRUE(lock2.owns_lock());

    // Ensure that locks for different sockets are independent
    EXPECT_NE(&lock1, &lock2);
}

TEST_F(SocketLockTest, ClearMutexes) {
    int socket1 = 1;

    // Acquire lock for socket1
    auto lock1 = socketLock.getLock(socket1);
    EXPECT_TRUE(lock1.owns_lock());

    // Clear all mutexes
    socketLock.clearMutexes();

    // Acquire lock for socket1 again after clearing
    auto lock2 = socketLock.getLock(socket1);
    EXPECT_TRUE(lock2.owns_lock());

    // Ensure that the new lock is valid and independent
    EXPECT_NE(&lock1, &lock2);
}
