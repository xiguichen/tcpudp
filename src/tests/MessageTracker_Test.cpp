#include "TcpVCWriteThread.h"
#include <gtest/gtest.h>

// MessageTracker maps recently sent message ids to the connection they were
// sent on, so a resend request can mark the right connection degraded. The map
// must stay bounded: a long-lived virtual channel sends an unbounded number of
// messages, but only the recent ones can still be found in SentDataCache and
// therefore be resent.
TEST(MessageTrackerTest, BoundedCapacityEvictsOldestEntries)
{
    MessageTracker tracker(1, 4);

    for (uint64_t id = 0; id < 10; ++id)
        tracker.recordMessage(id, static_cast<int>(id));

    EXPECT_EQ(tracker.getConnectionIndex(9), 9);
    EXPECT_EQ(tracker.getConnectionIndex(6), 6);
    EXPECT_EQ(tracker.getConnectionIndex(5), -1)
        << "entries older than the retention window must be evicted";
    EXPECT_EQ(tracker.getConnectionIndex(0), -1);
}

TEST(MessageTrackerTest, RecordsWithinCapacityAreRetained)
{
    MessageTracker tracker(1, 8);

    for (uint64_t id = 0; id < 8; ++id)
        tracker.recordMessage(id, static_cast<int>(id));

    for (uint64_t id = 0; id < 8; ++id)
        EXPECT_EQ(tracker.getConnectionIndex(id), static_cast<int>(id));
}

TEST(MessageTrackerTest, ClearRemovesAllEntries)
{
    MessageTracker tracker(1, 4);
    tracker.recordMessage(0, 0);
    tracker.recordMessage(1, 1);

    tracker.clear();

    EXPECT_EQ(tracker.getConnectionIndex(0), -1);
    EXPECT_EQ(tracker.getConnectionIndex(1), -1);
}
