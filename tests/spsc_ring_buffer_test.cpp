#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#include "oms/spsc_ring_buffer.hpp"

using namespace oms;

TEST(SpscRingBufferTest, BasicPushPopSingleThread) {
    SpscRingBuffer<int> q(3);
    EXPECT_EQ(q.capacity(), 3u);

    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4)); // full

    auto a = q.try_pop();
    auto b = q.try_pop();
    auto c = q.try_pop();
    auto d = q.try_pop();

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());
    EXPECT_FALSE(d.has_value());
    EXPECT_EQ(*a, 1);
    EXPECT_EQ(*b, 2);
    EXPECT_EQ(*c, 3);
}

TEST(SpscRingBufferTest, WrapAroundWorks) {
    SpscRingBuffer<int> q(2);
    ASSERT_TRUE(q.try_push(10));
    ASSERT_TRUE(q.try_push(20));
    ASSERT_FALSE(q.try_push(30));

    auto a = q.try_pop();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, 10);

    EXPECT_TRUE(q.try_push(30));
    EXPECT_FALSE(q.try_push(40)); // full again

    auto b = q.try_pop();
    auto c = q.try_pop();
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(*b, 20);
    EXPECT_EQ(*c, 30);
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(SpscRingBufferTest, StressTestSingleProducerSingleConsumer) {
    constexpr int kItems = 100000;
    SpscRingBuffer<int> q(1024);
    std::atomic<bool> producer_done{false};
    std::vector<int> consumed;
    consumed.reserve(kItems);

    std::thread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        while (consumed.size() < static_cast<std::size_t>(kItems)) {
            auto v = q.try_pop();
            if (v.has_value()) {
                consumed.push_back(*v);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(consumed.size(), static_cast<std::size_t>(kItems));
    for (int i = 0; i < kItems; ++i) {
        EXPECT_EQ(consumed[static_cast<std::size_t>(i)], i + 1);
        if (consumed[static_cast<std::size_t>(i)] != i + 1) {
            break;
        }
    }
}

TEST(SpscRingBufferTest, MoveOnlyType) {
    struct MoveOnly {
        int value;
        explicit MoveOnly(int v) : value(v) {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) noexcept = default;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;
    };

    SpscRingBuffer<MoveOnly> q(2);
    EXPECT_TRUE(q.try_push(MoveOnly{7}));
    EXPECT_TRUE(q.try_push(MoveOnly{8}));
    EXPECT_FALSE(q.try_push(MoveOnly{9}));

    auto a = q.try_pop();
    auto b = q.try_pop();
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a->value, 7);
    EXPECT_EQ(b->value, 8);
}