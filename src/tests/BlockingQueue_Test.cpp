#include "BlockingQueue.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// ─── Basic enqueue / dequeue ────────────────────────────────────────────────

TEST(BlockingQueueTest, EnqueueDequeueSingleItem)
{
    BlockingQueue q;
    auto data = std::make_shared<std::vector<char>>(std::vector<char>{'a', 'b', 'c'});
    q.enqueue(data);
    auto result = q.dequeue();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, *data);
}

TEST(BlockingQueueTest, EnqueueDequeueMultipleItemsFIFO)
{
    BlockingQueue q;
    for (int i = 0; i < 5; ++i)
    {
        auto data = std::make_shared<std::vector<char>>(std::vector<char>{static_cast<char>(i)});
        q.enqueue(data);
    }
    for (int i = 0; i < 5; ++i)
    {
        auto result = q.dequeue();
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->at(0), static_cast<char>(i));
    }
}

TEST(BlockingQueueTest, EnqueuePreservesDataContents)
{
    BlockingQueue q;
    std::vector<char> payload = {'h', 'e', 'l', 'l', 'o'};
    q.enqueue(std::make_shared<std::vector<char>>(payload));
    auto result = q.dequeue();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, payload);
}

// ─── Blocking behaviour ─────────────────────────────────────────────────────

// dequeue must block until a producer enqueues data
TEST(BlockingQueueTest, DequeueBlocksUntilEnqueue)
{
    BlockingQueue q;
    std::atomic<bool> received{false};

    std::thread consumer([&]() {
        auto result = q.dequeue();
        received = (result != nullptr);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(received.load()); // still blocked

    q.enqueue(std::make_shared<std::vector<char>>(std::vector<char>{'x'}));
    consumer.join();
    EXPECT_TRUE(received.load());
}

// ─── cancelWait ─────────────────────────────────────────────────────────────

// cancelWait unblocks a waiting dequeue and makes it return nullptr
TEST(BlockingQueueTest, CancelWaitUnblocksDequeue)
{
    BlockingQueue q;
    std::shared_ptr<std::vector<char>> result =
        std::make_shared<std::vector<char>>(std::vector<char>{1}); // non-null sentinel

    std::thread consumer([&]() { result = q.dequeue(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.cancelWait();
    consumer.join();

    EXPECT_EQ(result, nullptr);
}

// Once cancelled, dequeue returns nullptr immediately without blocking
TEST(BlockingQueueTest, CancelledQueueReturnsNullImmediately)
{
    BlockingQueue q;
    q.cancelWait();

    auto start = std::chrono::steady_clock::now();
    auto result = q.dequeue();
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(result, nullptr);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
}

// After cancelWait, dequeue returns nullptr even when data was already enqueued
TEST(BlockingQueueTest, CancelWaitTakesPriorityOverEnqueuedData)
{
    BlockingQueue q;
    q.enqueue(std::make_shared<std::vector<char>>(std::vector<char>{'z'}));
    q.cancelWait();

    auto result = q.dequeue();
    EXPECT_EQ(result, nullptr);
}

// cancelWait must unblock all concurrently waiting consumers
TEST(BlockingQueueTest, CancelWaitUnblocksMultipleConsumers)
{
    BlockingQueue q;
    const int N = 4;
    std::atomic<int> nullCount{0};
    std::vector<std::thread> consumers;

    for (int i = 0; i < N; ++i)
    {
        consumers.emplace_back([&]() {
            auto result = q.dequeue();
            if (result == nullptr)
                nullCount++;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.cancelWait();

    for (auto &t : consumers)
        t.join();

    EXPECT_EQ(nullCount.load(), N);
}

// ─── Concurrent producer / consumer ─────────────────────────────────────────

// All enqueued items must be received exactly once; no items dropped or duplicated
TEST(BlockingQueueTest, ConcurrentEnqueueDequeue)
{
    BlockingQueue q;
    const int N = 200;
    std::atomic<int> receivedCount{0};

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i)
        {
            auto data = std::make_shared<std::vector<char>>(std::vector<char>{static_cast<char>(i & 0xFF)});
            q.enqueue(data);
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < N; ++i)
        {
            auto result = q.dequeue();
            if (result != nullptr)
                receivedCount++;
        }
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(receivedCount.load(), N);
}

// Multiple producers feeding a single consumer
TEST(BlockingQueueTest, MultipleProducersSingleConsumer)
{
    BlockingQueue q;
    const int producers = 4;
    const int itemsPerProducer = 25;
    const int total = producers * itemsPerProducer;
    std::atomic<int> receivedCount{0};
    std::atomic<bool> cancelled{false};

    std::vector<std::thread> producerThreads;
    for (int p = 0; p < producers; ++p)
    {
        producerThreads.emplace_back([&]() {
            for (int i = 0; i < itemsPerProducer; ++i)
                q.enqueue(std::make_shared<std::vector<char>>(std::vector<char>{'x'}));
        });
    }

    std::thread consumer([&]() {
        while (receivedCount.load() < total)
        {
            auto result = q.dequeue();
            if (result != nullptr)
                receivedCount++;
        }
    });

    for (auto &t : producerThreads)
        t.join();
    consumer.join();

    EXPECT_EQ(receivedCount.load(), total);
}
