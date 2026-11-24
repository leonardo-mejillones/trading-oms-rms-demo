#include <gtest/gtest.h>

#include "oms/oms.hpp"
#include "oms/risk.hpp"

using namespace oms;

namespace {

Order make_limit(uint64_t id, int64_t price, uint64_t qty) {
    return Order(OrderId{id}, AccountId{1}, Side::Buy, Price{price}, Quantity{qty},
                 OrderType::Limit, TimeInForce::GTC);
}

} // namespace

TEST(RiskLimitsTest, MaxOrderSizeBlocksFatFinger) {
    Oms oms;
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<MaxOrderSizeCheck>(Quantity{100}));
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 50, 101), sink); // size 101 > 100

    EXPECT_FALSE(oms.book().best_bid().has_value());
    const auto rejected = sink.events_of_type<OrderRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].reason, "max order size exceeded");
}

TEST(RiskLimitsTest, MaxOrderSizeAllowsWithinLimit) {
    Oms oms;
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<MaxOrderSizeCheck>(Quantity{100}));
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 50, 100), sink);

    ASSERT_TRUE(oms.book().best_bid().has_value());
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);
}

TEST(RiskLimitsTest, MaxNotionalBlocksFatFinger) {
    Oms oms;
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<MaxNotionalCheck>(10'000)); // max notional 10000 ticks
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 200, 60), sink); // notional 12000 > 10000

    EXPECT_FALSE(oms.book().best_bid().has_value());
    const auto rejected = sink.events_of_type<OrderRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].reason, "max order notional exceeded");
}

TEST(RiskLimitsTest, MaxNotionalAllowsWithinLimit) {
    Oms oms;
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<MaxNotionalCheck>(12'000));
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 200, 60), sink); // notional 12000 == 12000

    ASSERT_TRUE(oms.book().best_bid().has_value());
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);
}
