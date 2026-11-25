#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "oms/order_flow_generator.hpp"

using namespace oms;

namespace {

bool same_request(const OrderRequest& a, const OrderRequest& b) {
    return a.id == b.id && a.account == b.account && a.client_order_id == b.client_order_id &&
           a.symbol == b.symbol && a.side == b.side && a.price == b.price && a.quantity == b.quantity &&
           a.type == b.type && a.tif == b.tif;
}

bool same_command(const EngineCommand& a, const EngineCommand& b) {
    if (a.index() != b.index()) return false;
    if (const auto* sa = std::get_if<SubmitCommand>(&a)) {
        const auto* sb = std::get_if<SubmitCommand>(&b);
        return sb != nullptr && same_request(sa->request, sb->request);
    }
    if (const auto* ca = std::get_if<CancelCommand>(&a)) {
        const auto* cb = std::get_if<CancelCommand>(&b);
        return cb != nullptr && ca->order_id == cb->order_id;
    }
    if (const auto* ra = std::get_if<ReplaceCommand>(&a)) {
        const auto* rb = std::get_if<ReplaceCommand>(&b);
        return rb != nullptr && ra->order_id == rb->order_id && ra->new_price == rb->new_price &&
               ra->new_quantity == rb->new_quantity;
    }
    return false;
}

} // namespace

TEST(OrderFlowGeneratorTest, ReproducibleForSameSeedAndConfig) {
    OrderFlowConfig config;
    config.seed = 42;
    config.count = 200;

    OrderFlowGenerator gen;
    const auto a = gen.generate(config);
    const auto b = gen.generate(config);

    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(same_command(a[i], b[i])) << "difference at index " << i;
    }
}

TEST(OrderFlowGeneratorTest, ContainsNewAggressiveAndCancelOrders) {
    OrderFlowConfig config;
    config.seed = 123;
    config.count = 500;
    config.new_weight = 60;
    config.aggressive_weight = 25;
    config.cancel_weight = 15;

    OrderFlowGenerator gen;
    const auto commands = gen.generate(config);

    std::size_t passive_new = 0;
    std::size_t aggressive_new = 0;
    std::size_t cancels = 0;

    for (const auto& cmd : commands) {
        if (std::holds_alternative<CancelCommand>(cmd)) {
            ++cancels;
            continue;
        }
        const auto& submit = std::get<SubmitCommand>(cmd);
        const bool aggressive = (submit.request.side == Side::Buy && submit.request.price == Price{config.max_price}) ||
                                (submit.request.side == Side::Sell && submit.request.price == Price{config.min_price});
        if (aggressive) {
            ++aggressive_new;
        } else {
            ++passive_new;
        }
    }

    EXPECT_GT(passive_new, 0u);
    EXPECT_GT(aggressive_new, 0u);
    EXPECT_GT(cancels, 0u);
    EXPECT_EQ(passive_new + aggressive_new + cancels, commands.size());
}

TEST(OrderFlowGeneratorTest, GeneratedCommandsCanBeProcessedDeterministically) {
    OrderFlowConfig config;
    config.seed = 7;
    config.count = 300;

    OrderFlowGenerator gen;
    const auto commands = gen.generate(config);

    MatchingEngine e1({{"AAPL", InstrumentSpec{"AAPL", 1, 1}}});
    MatchingEngine e2({{"AAPL", InstrumentSpec{"AAPL", 1, 1}}});
    InMemoryEventSink s1, s2;

    for (const auto& cmd : commands) {
        e1.process(cmd, s1);
        e2.process(cmd, s2);
    }

    ASSERT_EQ(s1.events().size(), s2.events().size());
    for (std::size_t i = 0; i < s1.events().size(); ++i) {
        EXPECT_EQ(s1.events()[i].index(), s2.events()[i].index()) << "index " << i;
    }
    EXPECT_EQ(e1.book().best_bid(), e2.book().best_bid());
    EXPECT_EQ(e1.book().best_ask(), e2.book().best_ask());
}