#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "oms/oms.hpp"
#include "oms/risk.hpp"

using namespace oms;

namespace {

Order make_order(uint64_t id, uint64_t account, uint64_t client_order_id = 0) {
    return Order(OrderId{id}, AccountId{account}, Side::Buy, Price{100}, Quantity{1},
                 OrderType::Limit, TimeInForce::GTC, ClientOrderId{client_order_id});
}

} // namespace

TEST(RateLimitTest, AllowsBurstThenThrottles) {
    auto now = std::chrono::steady_clock::time_point{};
    RateLimitCheck check(2, 0.0, [&now]() { return now; }); // capacity 2, no refill

    EXPECT_TRUE(check.check(make_order(1, 1)).ok);
    EXPECT_TRUE(check.check(make_order(2, 1)).ok);
    const auto rejected = check.check(make_order(3, 1));
    EXPECT_FALSE(rejected.ok);
    EXPECT_EQ(rejected.reason, "rate limit exceeded");
}

TEST(RateLimitTest, RefillsOverTimeDeterministically) {
    auto now = std::chrono::steady_clock::time_point{};
    RateLimitCheck check(1, 1.0, [&now]() { return now; }); // 1 token/sec

    EXPECT_TRUE(check.check(make_order(1, 1)).ok);
    EXPECT_FALSE(check.check(make_order(2, 1)).ok);

    now += std::chrono::milliseconds(500);
    EXPECT_FALSE(check.check(make_order(3, 1)).ok); // only 0.5 token refilled

    now += std::chrono::milliseconds(500);
    EXPECT_TRUE(check.check(make_order(4, 1)).ok); // 1.0 token refilled
    EXPECT_FALSE(check.check(make_order(5, 1)).ok);
}

TEST(RateLimitTest, IsPerAccount) {
    auto now = std::chrono::steady_clock::time_point{};
    RateLimitCheck check(1, 0.0, [&now]() { return now; });

    EXPECT_TRUE(check.check(make_order(1, 1)).ok);
    EXPECT_FALSE(check.check(make_order(2, 1)).ok); // account 1 throttled

    EXPECT_TRUE(check.check(make_order(3, 2)).ok);  // account 2 independent
    EXPECT_FALSE(check.check(make_order(4, 2)).ok);
}

TEST(RateLimitTest, RejectedOrderDoesNotReachBook) {
    auto now = std::chrono::steady_clock::time_point{};
    auto limiter = std::make_shared<RateLimitCheck>(1, 0.0, [&now]() { return now; });
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(limiter);

    Oms oms;
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_order(1, 1, 100), sink);
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);

    sink.clear();
    oms.submit(make_order(2, 1, 101), sink); // same account, limit exhausted
    ASSERT_EQ(sink.events_of_type<OrderRejected>().size(), 1u);
    EXPECT_EQ(sink.events_of_type<OrderRejected>()[0].reason, "rate limit exceeded");
    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{100}), Quantity{1});
}

TEST(RateLimitTest, CapacityCapsRefill) {
    auto now = std::chrono::steady_clock::time_point{};
    RateLimitCheck check(2, 10.0, [&now]() { return now; }); // fast refill, cap at 2

    EXPECT_TRUE(check.check(make_order(1, 1)).ok);
    EXPECT_TRUE(check.check(make_order(2, 1)).ok);
    EXPECT_FALSE(check.check(make_order(3, 1)).ok);

    now += std::chrono::seconds(10); // would refill 100 tokens, but capped at 2
    EXPECT_TRUE(check.check(make_order(4, 1)).ok);
    EXPECT_TRUE(check.check(make_order(5, 1)).ok);
    EXPECT_FALSE(check.check(make_order(6, 1)).ok);
}
