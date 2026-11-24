#include <gtest/gtest.h>

#include <memory>

#include "oms/oms.hpp"
#include "oms/position.hpp"

using namespace oms;

namespace {

Order make_limit(uint64_t id, uint64_t account, Side side, int64_t price, uint64_t qty,
                 std::string symbol = "AAPL") {
    return Order(OrderId{id}, AccountId{account}, side, Price{price}, Quantity{qty},
                 OrderType::Limit, TimeInForce::GTC, ClientOrderId{0}, std::move(symbol));
}

} // namespace

TEST(PositionTest, PositionsUpdatedFromFillsLongAndShort) {
    auto tracker = std::make_shared<PositionTracker>();
    Oms oms;
    oms.set_position_tracker(tracker);

    InMemoryEventSink sink;

    // Resting sell, then buy crosses it -> buy filled long, sell filled short.
    oms.submit(make_limit(1, 2, Side::Sell, 100, 10), sink);
    sink.clear();
    oms.submit(make_limit(2, 1, Side::Buy, 100, 10), sink);

    EXPECT_EQ(tracker->position(AccountId{1}, "AAPL"), 10);   // long
    EXPECT_EQ(tracker->position(AccountId{2}, "AAPL"), -10);  // short
}

TEST(PositionTest, PositionLimitConsidersOpenOrdersWorstCaseLong) {
    auto tracker = std::make_shared<PositionTracker>();
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<PositionLimitCheck>(*tracker, 10, 10));

    Oms oms;
    oms.set_position_tracker(tracker);
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 1, Side::Buy, 100, 6), sink); // open buy 6
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);

    sink.clear();
    oms.submit(make_limit(2, 1, Side::Buy, 100, 5), sink); // worst case 11 > 10
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 0u);
    ASSERT_EQ(sink.events_of_type<OrderRejected>().size(), 1u);
    EXPECT_EQ(sink.events_of_type<OrderRejected>()[0].reason, "position limit exceeded");

    // Still within limit.
    sink.clear();
    oms.submit(make_limit(3, 1, Side::Buy, 100, 4), sink);
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);
}

TEST(PositionTest, PositionLimitConsidersOpenOrdersWorstCaseShort) {
    auto tracker = std::make_shared<PositionTracker>();
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<PositionLimitCheck>(*tracker, 10, 10));

    Oms oms;
    oms.set_position_tracker(tracker);
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 1, Side::Sell, 100, 6), sink); // open sell 6
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);

    sink.clear();
    oms.submit(make_limit(2, 1, Side::Sell, 100, 5), sink); // worst case -11 < -10
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 0u);
    ASSERT_EQ(sink.events_of_type<OrderRejected>().size(), 1u);
    EXPECT_EQ(sink.events_of_type<OrderRejected>()[0].reason, "position limit exceeded");
}

TEST(PositionTest, FilledPositionReducesOpenExposure) {
    auto tracker = std::make_shared<PositionTracker>();
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<PositionLimitCheck>(*tracker, 10, 10));

    Oms oms;
    oms.set_position_tracker(tracker);
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    // Account 1 buys 10 from account 2 (fully filled).
    oms.submit(make_limit(1, 2, Side::Sell, 100, 10), sink);
    oms.submit(make_limit(2, 1, Side::Buy, 100, 10), sink);
    EXPECT_EQ(tracker->position(AccountId{1}, "AAPL"), 10);
    EXPECT_EQ(tracker->open_buy(AccountId{1}, "AAPL"), 0u);

    // Now account 1 can sell 10 (flattens) and another sell 10 up to short limit.
    sink.clear();
    oms.submit(make_limit(3, 1, Side::Sell, 101, 20), sink);
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u); // projected -10 ok
}
