#include <gtest/gtest.h>

#include "oms/oms.hpp"
#include "oms/event_sink.hpp"

using namespace oms;

namespace {

Order make_order(uint64_t id, uint64_t account, uint64_t client_id, Side side, int64_t price, uint64_t qty) {
    return Order(OrderId{id}, AccountId{account}, side, Price{price}, Quantity{qty},
                 OrderType::Limit, TimeInForce::GTC, ClientOrderId{client_id});
}

} // namespace

TEST(IdempotencyTest, DuplicateClientOrderIdReturnsOriginalResult) {
    Oms oms;
    InMemoryEventSink sink1;
    InMemoryEventSink sink2;

    Order first = make_order(1, 10, 100, Side::Buy, 50, 5);
    Order duplicate = make_order(2, 10, 100, Side::Buy, 50, 5); // same account+client id, different order id

    oms.submit(first, sink1);
    oms.submit(duplicate, sink2);

    // Original result is replayed without reprocessing.
    ASSERT_EQ(sink1.events().size(), sink2.events().size());
    EXPECT_EQ(sink1.events(), sink2.events());

    // Book should contain only the original order, not a duplicate.
    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{50}), Quantity{5});
    ASSERT_TRUE(oms.book().best_bid().has_value());
    EXPECT_EQ(*oms.book().best_bid(), Price{50});
}

TEST(IdempotencyTest, SameClientOrderIdDifferentAccountIsNotDuplicate) {
    Oms oms;
    InMemoryEventSink sink1;
    InMemoryEventSink sink2;

    oms.submit(make_order(1, 10, 100, Side::Buy, 50, 5), sink1);
    oms.submit(make_order(2, 11, 100, Side::Buy, 50, 5), sink2); // different account

    EXPECT_EQ(oms.book().depth_at(Side::Buy, Price{50}), Quantity{10});
}

TEST(IdempotencyTest, DuplicateCancelIsIdempotent) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(Order(OrderId{1}, AccountId{1}, Side::Buy, Price{100}, Quantity{10}));

    book.cancel(OrderId{1}, sink);
    ASSERT_EQ(sink.events_of_type<OrderCanceled>().size(), 1u);
    EXPECT_TRUE(sink.events_of_type<CancelRejected>().empty());

    sink.clear();
    book.cancel(OrderId{1}, sink); // duplicate cancel

    const auto canceled = sink.events_of_type<OrderCanceled>();
    ASSERT_EQ(canceled.size(), 1u);
    EXPECT_EQ(canceled[0].order_id, OrderId{1});
    EXPECT_TRUE(sink.events_of_type<CancelRejected>().empty());
}

TEST(IdempotencyTest, CancelAfterFillIsStillRejected) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(Order(OrderId{1}, AccountId{1}, Side::Sell, Price{100}, Quantity{10}));
    book.submit(Order(OrderId{2}, AccountId{2}, Side::Buy, Price{100}, Quantity{10}), sink);

    sink.clear();
    book.cancel(OrderId{1}, sink); // filled, not canceled

    EXPECT_TRUE(sink.events_of_type<OrderCanceled>().empty());
    ASSERT_EQ(sink.events_of_type<CancelRejected>().size(), 1u);
}
