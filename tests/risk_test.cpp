#include <gtest/gtest.h>

#include <memory>

#include "oms/oms.hpp"
#include "oms/risk.hpp"

using namespace oms;

namespace {

class AlwaysPassCheck : public RiskCheck {
public:
    explicit AlwaysPassCheck(int& counter) : counter_(counter) {}
    RiskResult check(const Order&) const override {
        ++counter_;
        return RiskResult::pass();
    }
private:
    int& counter_;
};

class RejectCheck : public RiskCheck {
public:
    explicit RejectCheck(std::string reason, int& counter) : reason_(std::move(reason)), counter_(counter) {}
    RiskResult check(const Order&) const override {
        ++counter_;
        return RiskResult::reject(reason_);
    }
private:
    std::string reason_;
    int& counter_;
};

Order make_order(uint64_t id, Side side = Side::Buy, int64_t price = 100, uint64_t qty = 10) {
    return Order(OrderId{id}, AccountId{1}, side, Price{price}, Quantity{qty});
}

} // namespace

TEST(RiskPipelineTest, PassesWhenAllChecksPass) {
    int c1 = 0, c2 = 0;
    RiskCheckPipeline pipeline;
    pipeline.add(std::make_shared<AlwaysPassCheck>(c1));
    pipeline.add(std::make_shared<AlwaysPassCheck>(c2));

    const auto result = pipeline.evaluate(make_order(1));
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.reason.empty());
    EXPECT_EQ(c1, 1);
    EXPECT_EQ(c2, 1);
}

TEST(RiskPipelineTest, ShortCircuitsOnFirstRejection) {
    int c1 = 0, c2 = 0, c3 = 0;
    RiskCheckPipeline pipeline;
    pipeline.add(std::make_shared<AlwaysPassCheck>(c1));
    pipeline.add(std::make_shared<RejectCheck>("risk limit breached", c2));
    pipeline.add(std::make_shared<AlwaysPassCheck>(c3));

    const auto result = pipeline.evaluate(make_order(1));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.reason, "risk limit breached");
    EXPECT_EQ(c1, 1);
    EXPECT_EQ(c2, 1);
    EXPECT_EQ(c3, 0); // short-circuited
}

TEST(RiskPipelineTest, RejectedOrderDoesNotReachBook) {
    Oms oms;
    int c1 = 0;
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<RejectCheck>("not allowed", c1));
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_order(1, Side::Buy, 100, 10), sink);

    EXPECT_EQ(c1, 1);
    EXPECT_FALSE(oms.book().best_bid().has_value());
    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{100}), Quantity{0});

    const auto rejected = sink.events_of_type<OrderRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].order_id, OrderId{1});
    EXPECT_EQ(rejected[0].reason, "not allowed");
}

TEST(RiskPipelineTest, PassingPipelineAllowsOrderToReachBook) {
    Oms oms;
    int c1 = 0;
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<AlwaysPassCheck>(c1));
    oms.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    oms.submit(make_order(1, Side::Buy, 100, 10), sink);

    EXPECT_EQ(c1, 1);
    ASSERT_TRUE(oms.book().best_bid().has_value());
    EXPECT_EQ(*oms.book().best_bid(), Price{100});
    EXPECT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);
}
