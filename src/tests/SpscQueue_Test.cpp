#include "SpscQueue.h"
#include <gtest/gtest.h>

TEST(SpscQueueTest, IsFullMatchesTryEnqueue)
{
    // One slot is reserved to distinguish full from empty, so a Capacity-N
    // queue accepts N-1 entries.
    SpscQueue<int, 4> queue;
    EXPECT_FALSE(queue.isFull());
    EXPECT_TRUE(queue.empty());

    EXPECT_TRUE(queue.try_enqueue(1));
    EXPECT_FALSE(queue.isFull());
    EXPECT_TRUE(queue.try_enqueue(2));
    EXPECT_TRUE(queue.try_enqueue(3));

    // Full: the next enqueue must fail without mutating the queue.
    EXPECT_TRUE(queue.isFull());
    EXPECT_FALSE(queue.try_enqueue(4));

    int value = 0;
    ASSERT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 1);
    EXPECT_FALSE(queue.isFull());

    EXPECT_TRUE(queue.try_enqueue(5));

    ASSERT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 2);
    ASSERT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 3);
    ASSERT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 5);
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.try_dequeue(value));
}
