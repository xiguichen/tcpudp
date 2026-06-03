#include "ReplaceSlotPolicy.h"
#include <gtest/gtest.h>

// connectionId resolved a slot: always honor it.
TEST(DecideReplaceSlot, UsesConnIdSlotWhenFound)
{
    EXPECT_EQ(DecideReplaceSlot(/*connIdSlot=*/7, /*slotIndex=*/3, /*numSlots=*/32, /*deadSlots=*/{}), 7);
}

// The macOS bug case: connectionId missed (stale map) AND no dead slots observed,
// but the client told us which slot it is reconnecting. Trust slotIndex instead of
// rejecting the socket as a stray.
TEST(DecideReplaceSlot, TrustsClientSlotIndexWhenConnIdMissesAndNoDeadSlots)
{
    EXPECT_EQ(DecideReplaceSlot(/*connIdSlot=*/-1, /*slotIndex=*/5, /*numSlots=*/32, /*deadSlots=*/{}), 5);
}

// connId missed and the slotIndex is out of range / absent: fall back to an observed dead slot.
TEST(DecideReplaceSlot, FallsBackToDeadSlotWhenNoUsableSlotIndex)
{
    EXPECT_EQ(DecideReplaceSlot(/*connIdSlot=*/-1, /*slotIndex=*/-1, /*numSlots=*/32, /*deadSlots=*/{4, 9}), 4);
}

// Nothing usable: reject as a stray (-1) so the caller closes the socket.
TEST(DecideReplaceSlot, RejectsWhenNothingResolvable)
{
    EXPECT_EQ(DecideReplaceSlot(/*connIdSlot=*/-1, /*slotIndex=*/-1, /*numSlots=*/32, /*deadSlots=*/{}), -1);
}

// An out-of-range slotIndex must not be trusted.
TEST(DecideReplaceSlot, IgnoresOutOfRangeSlotIndex)
{
    EXPECT_EQ(DecideReplaceSlot(/*connIdSlot=*/-1, /*slotIndex=*/99, /*numSlots=*/32, /*deadSlots=*/{2}), 2);
    EXPECT_EQ(DecideReplaceSlot(/*connIdSlot=*/-1, /*slotIndex=*/99, /*numSlots=*/32, /*deadSlots=*/{}), -1);
}
