#include "ReorderBuffer.h"
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
using Buffer = std::shared_ptr<std::vector<char>>;

Buffer MakePayload(const std::string &text)
{
    return std::make_shared<std::vector<char>>(text.begin(), text.end());
}
} // namespace

TEST(ReorderBufferTest, DeliversBufferedPacketsInOrder)
{
    ReorderBuffer buffer(16);

    buffer.insert(2, MakePayload("two"), 2);
    buffer.insert(0, MakePayload("zero"), 0);
    buffer.insert(1, MakePayload("one"), 1);

    EXPECT_EQ(buffer.size(), 3u);

    std::vector<ReorderBuffer::Item> out;
    EXPECT_EQ(buffer.drain(out), 3u);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].messageId, 0u);
    EXPECT_EQ(std::string(out[0].data->begin(), out[0].data->end()), "zero");
    EXPECT_EQ(out[0].sourceConnIndex, 0);
    EXPECT_EQ(out[1].messageId, 1u);
    EXPECT_EQ(out[2].messageId, 2u);
    EXPECT_EQ(buffer.nextId(), 3u);
    EXPECT_TRUE(buffer.empty());
}

TEST(ReorderBufferTest, HoldsPacketsUntilTheGapIsFilled)
{
    ReorderBuffer buffer(16);

    buffer.insert(2, MakePayload("two"), 0);

    std::vector<ReorderBuffer::Item> out;
    EXPECT_EQ(buffer.drain(out), 0u);
    EXPECT_EQ(buffer.nextId(), 0u);
    EXPECT_EQ(buffer.size(), 1u);

    buffer.insert(0, MakePayload("zero"), 0);
    buffer.insert(1, MakePayload("one"), 0);

    EXPECT_EQ(buffer.drain(out), 3u);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].messageId, 0u);
    EXPECT_EQ(out[2].messageId, 2u);
    EXPECT_TRUE(buffer.empty());
}

TEST(ReorderBufferTest, RejectsDuplicatesAndAlreadyDeliveredIds)
{
    ReorderBuffer buffer(16);

    EXPECT_TRUE(buffer.insert(0, MakePayload("zero"), 0));
    EXPECT_FALSE(buffer.insert(0, MakePayload("duplicate"), 0));
    EXPECT_EQ(buffer.size(), 1u);

    std::vector<ReorderBuffer::Item> out;
    buffer.drain(out);
    ASSERT_EQ(out.size(), 1u);

    EXPECT_FALSE(buffer.insert(0, MakePayload("old"), 0));
    EXPECT_TRUE(buffer.empty());
}

TEST(ReorderBufferTest, SkipToDropsMissingIds)
{
    ReorderBuffer buffer(16);

    buffer.insert(5, MakePayload("five"), 0);

    buffer.skipTo(5);
    EXPECT_EQ(buffer.nextId(), 5u);

    std::vector<ReorderBuffer::Item> out;
    EXPECT_EQ(buffer.drain(out), 1u);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].messageId, 5u);
    EXPECT_EQ(buffer.nextId(), 6u);
}

TEST(ReorderBufferTest, GrowsWhenAnIdIsFurtherAheadThanCapacity)
{
    ReorderBuffer buffer(4);

    buffer.insert(0, MakePayload("zero"), 0);
    EXPECT_EQ(buffer.nextId(), 0u);

    // id 10 is further ahead than the initial 4-slot window. The window grows
    // rather than dropping the packets already buffered behind the gap.
    buffer.insert(10, MakePayload("ten"), 0);
    EXPECT_EQ(buffer.nextId(), 0u);
    EXPECT_EQ(buffer.size(), 2u);

    buffer.insert(5, MakePayload("five"), 0);
    buffer.insert(1, MakePayload("one"), 0);
    EXPECT_EQ(buffer.size(), 4u);

    std::vector<ReorderBuffer::Item> out;
    EXPECT_EQ(buffer.drain(out), 2u); // 0, 1
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].messageId, 0u);
    EXPECT_EQ(out[1].messageId, 1u);

    buffer.skipTo(5);
    out.clear();
    EXPECT_EQ(buffer.drain(out), 1u); // 5 (10 still has a gap before it)
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].messageId, 5u);

    buffer.skipTo(10);
    out.clear();
    EXPECT_EQ(buffer.drain(out), 1u); // 10
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].messageId, 10u);
    EXPECT_TRUE(buffer.empty());
}

TEST(ReorderBufferTest, ReusesSlotsAfterWrapAround)
{
    ReorderBuffer buffer(4);

    buffer.insert(0, MakePayload("zero"), 0);
    std::vector<ReorderBuffer::Item> out;
    buffer.drain(out);
    ASSERT_EQ(out.size(), 1u);
    out.clear();

    // id 4 maps to the same slot as id 0, which was already delivered.
    buffer.insert(4, MakePayload("four"), 0);
    buffer.insert(1, MakePayload("one"), 0);
    buffer.insert(2, MakePayload("two"), 0);
    buffer.insert(3, MakePayload("three"), 0);

    EXPECT_EQ(buffer.drain(out), 4u);
    ASSERT_EQ(out.size(), 4u);
    for (uint64_t i = 0; i < 4; ++i)
        EXPECT_EQ(out[i].messageId, i + 1);
    EXPECT_TRUE(buffer.empty());
}

TEST(ReorderBufferTest, ForEachVisitsBufferedItemsInAscendingOrder)
{
    ReorderBuffer buffer(16);

    buffer.insert(5, MakePayload("five"), 0);
    buffer.insert(1, MakePayload("one"), 0);
    buffer.insert(3, MakePayload("three"), 0);
    buffer.insert(0, MakePayload("zero"), 0);

    std::vector<uint64_t> seen;
    buffer.forEach([&seen](uint64_t id, const Buffer &, int) { seen.push_back(id); });

    EXPECT_EQ(seen, (std::vector<uint64_t>{0, 1, 3, 5}));
}

TEST(ReorderBufferTest, RejectsIdsBeyondTheMaximumWindow)
{
    ReorderBuffer buffer(4);

    // A corrupt/absurd id must not make the window allocate unboundedly. The
    // packet is rejected and the stream stays parked until the reorder timeout
    // (or a resend) recovers it, matching the previous map behaviour.
    EXPECT_FALSE(buffer.insert((1ULL << 20) + 5, MakePayload("bad"), 0));
    EXPECT_TRUE(buffer.empty());

    // The buffer still works normally afterwards.
    EXPECT_TRUE(buffer.insert(0, MakePayload("zero"), 0));
    std::vector<ReorderBuffer::Item> out;
    EXPECT_EQ(buffer.drain(out), 1u);
}

TEST(ReorderBufferTest, ClearRemovesAllItemsAndResetsCursor)
{
    ReorderBuffer buffer(16);

    buffer.insert(0, MakePayload("zero"), 0);
    buffer.insert(2, MakePayload("two"), 0);
    buffer.clear();

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.nextId(), 0u);

    std::vector<ReorderBuffer::Item> out;
    EXPECT_EQ(buffer.drain(out), 0u);
}
