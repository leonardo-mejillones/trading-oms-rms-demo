#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "oms/matching_engine.hpp"

using namespace oms;

namespace {

class CountingPassCheck : public RiskCheck {
public:
    explicit CountingPassCheck(int& counter) : counter_(counter) {}
    RiskResult check(const Order&) const override {
        ++counter_;
        return RiskResult::pass();
    }

private:
    int& counter_;
};

std::unordered_map<std::string, InstrumentSpec> test_instruments() {
    return {
        {"AAPL", InstrumentSpec{"AAPL", 1, 1}},
    };
}

OrderRequest make_request(uint64_t id, uint64_t account, Side side, int64_t price, uint64_t qty,
                          std::string symbol = "AAPL", uint64_t client_order_id = 0,
                          OrderType type = OrderType::Limit, TimeInForce tif = TimeInForce::GTC) {
    OrderRequest req;
    req.id = OrderId{id};
    req.account = AccountId{account};
    req.client_order_id = ClientOrderId{client_order_id};
    req.symbol = std::move(symbol);
    req.side = side;
    req.price = Price{price};
    req.quantity = Quantity{qty};
    req.type = type;
    req.tif = tif;
    return req;
}

std::vector<Event> run_sequence(const std::vector<EngineCommand>& commands) {
    MatchingEngine engine(test_instruments());
    InMemoryEventSink sink;
    for (const auto& cmd : commands) {
        engine.process(cmd, sink);
    }
    return sink.events();
}

} // namespace

TEST(MatchingEngineTest, ProcessesCommandsInOrderThroughSingleEntryPoint) {
    MatchingEngine engine(test_instruments());
    InMemoryEventSink sink;

    engine.process(SubmitCommand{make_request(1, 1, Side::Sell, 100, 10)}, sink);
    engine.process(SubmitCommand{make_request(2, 2, Side::Buy, 100, 10)}, sink);
    engine.process(CancelCommand{OrderId{99}}, sink); // unknown, but processed in sequence

    const auto accepted = sink.events_of_type<OrderAccepted>();
    const auto fills = sink.events_of_type<Fill>();
    const auto cancel_rejected = sink.events_of_type<CancelRejected>();

    ASSERT_EQ(accepted.size(), 2u);
    ASSERT_EQ(fills.size(), 2u);
    ASSERT_EQ(cancel_rejected.size(), 1u);
    EXPECT_EQ(cancel_rejected[0].order_id, OrderId{99});
}

TEST(MatchingEngineTest, ValidationRejectsBeforeRiskAndMatching) {
    int risk_calls = 0;
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<CountingPassCheck>(risk_calls));

    MatchingEngine engine({{"AAPL", InstrumentSpec{"AAPL", 10, 1}}});
    engine.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    // Tick size is 10, so price 101 is invalid.
    engine.process(SubmitCommand{make_request(1, 1, Side::Buy, 101, 10)}, sink);

    const auto rejected = sink.events_of_type<OrderRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].reason, "InvalidTickSize");
    EXPECT_EQ(risk_calls, 0); // validation happened before risk
    EXPECT_FALSE(engine.book().best_bid().has_value());
}

TEST(MatchingEngineTest, RiskRejectsBeforeMatching) {
    MatchingEngine engine(test_instruments());
    auto pipeline = std::make_shared<RiskCheckPipeline>();
    pipeline->add(std::make_shared<MaxOrderSizeCheck>(Quantity{5}));
    engine.set_risk_pipeline(pipeline);

    InMemoryEventSink sink;
    engine.process(SubmitCommand{make_request(1, 1, Side::Buy, 100, 10)}, sink);

    const auto rejected = sink.events_of_type<OrderRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].reason, "max order size exceeded");
    EXPECT_FALSE(engine.book().best_bid().has_value());
}

TEST(MatchingEngineTest, DeterministicOutputForSameInput) {
    const std::vector<EngineCommand> commands = {
        SubmitCommand{make_request(1, 1, Side::Sell, 101, 5)},
        SubmitCommand{make_request(2, 2, Side::Sell, 100, 5)},
        SubmitCommand{make_request(3, 3, Side::Buy, 101, 8)}, // sweeps ask 100 then partial 101
        CancelCommand{OrderId{1}},
        ReplaceCommand{OrderId{2}, Price{100}, Quantity{2}},
    };

    const auto events1 = run_sequence(commands);
    const auto events2 = run_sequence(commands);

    ASSERT_EQ(events1.size(), events2.size());
    for (std::size_t i = 0; i < events1.size(); ++i) {
        EXPECT_EQ(events1[i].index(), events2[i].index()) << "event index differs at " << i;
        // Compare common event payloads.
        if (const auto* a = std::get_if<OrderAccepted>(&events1[i])) {
            EXPECT_EQ(*a, std::get<OrderAccepted>(events2[i]));
        } else if (const auto* r = std::get_if<OrderRejected>(&events1[i])) {
            EXPECT_EQ(*r, std::get<OrderRejected>(events2[i]));
        } else if (const auto* f = std::get_if<Fill>(&events1[i])) {
            EXPECT_EQ(*f, std::get<Fill>(events2[i]));
        } else if (const auto* c = std::get_if<OrderCanceled>(&events1[i])) {
            EXPECT_EQ(*c, std::get<OrderCanceled>(events2[i]));
        } else if (const auto* cr = std::get_if<CancelRejected>(&events1[i])) {
            EXPECT_EQ(*cr, std::get<CancelRejected>(events2[i]));
        } else {
            FAIL() << "unexpected event variant";
        }
    }

    // Also verify final book state is identical.
    MatchingEngine e1(test_instruments());
    MatchingEngine e2(test_instruments());
    InMemoryEventSink s1, s2;
    for (const auto& cmd : commands) {
        e1.process(cmd, s1);
        e2.process(cmd, s2);
    }
    EXPECT_EQ(e1.book().best_bid(), e2.book().best_bid());
    EXPECT_EQ(e1.book().best_ask(), e2.book().best_ask());
    EXPECT_EQ(e1.book().depth_at(Side::Buy, Price{101}), e2.book().depth_at(Side::Buy, Price{101}));
    EXPECT_EQ(e1.book().depth_at(Side::Sell, Price{100}), e2.book().depth_at(Side::Sell, Price{100}));
}
