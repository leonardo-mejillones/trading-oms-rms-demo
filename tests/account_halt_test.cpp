#include <gtest/gtest.h>

#include "oms/oms.hpp"

using namespace oms;

namespace {

Order make_limit(uint64_t id, uint64_t account, Side side, int64_t price, uint64_t qty,
                 uint64_t client_order_id = 0) {
    return Order(OrderId{id}, AccountId{account}, side, Price{price}, Quantity{qty},
                 OrderType::Limit, TimeInForce::GTC, ClientOrderId{client_order_id});
}

} // namespace

TEST(AccountHaltTest, HaltedAccountNewOrdersRejected) {
    Oms oms;
    oms.halt_account(AccountId{1});

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 1, Side::Buy, 100, 10), sink);

    EXPECT_TRUE(oms.book().best_bid().has_value() == false);
    ASSERT_EQ(sink.events_of_type<OrderRejected>().size(), 1u);
    EXPECT_EQ(sink.events_of_type<OrderRejected>()[0].reason, "account halted");
    EXPECT_TRUE(oms.is_account_halted(AccountId{1}));
}

TEST(AccountHaltTest, OtherAccountsUnaffected) {
    Oms oms;
    oms.halt_account(AccountId{1});

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 2, Side::Buy, 100, 10), sink);

    ASSERT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);
    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{100}), Quantity{10});
}

TEST(AccountHaltTest, CancelsStillAllowedWhenHalted) {
    Oms oms;
    InMemoryEventSink sink;

    oms.submit(make_limit(1, 1, Side::Buy, 100, 10), sink);
    ASSERT_EQ(oms.book().depth_at(Side::Buy, Price{100}), Quantity{10});

    oms.halt_account(AccountId{1});
    sink.clear();
    oms.cancel(OrderId{1}, sink);

    ASSERT_EQ(sink.events_of_type<OrderCanceled>().size(), 1u);
    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{100}), Quantity{0});
}

TEST(AccountHaltTest, ResumeAllowsNewOrders) {
    Oms oms;
    oms.halt_account(AccountId{1});
    oms.resume_account(AccountId{1});

    InMemoryEventSink sink;
    oms.submit(make_limit(1, 1, Side::Buy, 100, 10), sink);

    ASSERT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);
    EXPECT_FALSE(oms.is_account_halted(AccountId{1}));
}

TEST(AccountHaltTest, ReplaceRejectedWhenHalted) {
    Oms oms;
    InMemoryEventSink sink;

    oms.submit(make_limit(1, 1, Side::Buy, 100, 10), sink);
    oms.halt_account(AccountId{1});

    sink.clear();
    oms.replace(OrderId{1}, Price{101}, Quantity{10}, sink);

    ASSERT_EQ(sink.events_of_type<OrderRejected>().size(), 1u);
    EXPECT_EQ(sink.events_of_type<OrderRejected>()[0].reason, "account halted");
    // Original order remains unchanged.
    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{100}), Quantity{10});
    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{101}), Quantity{0});
}
