#include <gtest/gtest.h>

#include "oms/order_book.hpp"
#include "oms/event_sink.hpp"

using namespace oms;

namespace {

Order make_order(uint64_t id, Side side, int64_t price_ticks, uint64_t qty,
                 TimeInForce tif = TimeInForce::GTC) {
    return Order(OrderId{id}, AccountId{1}, side, Price{price_ticks}, Quantity{qty}, OrderType::Limit, tif);
}

} // namespace

TEST(CancelTest, CancelRestingOrderRemovesItFromBook) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Buy, 100, 10));
    ASSERT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{10});

    book.cancel(OrderId{1}, sink);

    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{0});

    const auto canceled = sink.events_of_type<OrderCanceled>();
    ASSERT_EQ(canceled.size(), 1u);
    EXPECT_EQ(canceled[0].order_id, OrderId{1});
    EXPECT_TRUE(sink.events_of_type<CancelRejected>().empty());
}

TEST(CancelTest, CancelUnknownOrderReturnsCancelRejected) {
    OrderBook book;
    InMemoryEventSink sink;

    book.cancel(OrderId{999}, sink);

    const auto rejected = sink.events_of_type<CancelRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].order_id, OrderId{999});
    EXPECT_FALSE(rejected[0].reason.empty());
    EXPECT_TRUE(sink.events_of_type<OrderCanceled>().empty());
}

TEST(CancelTest, CancelAlreadyFilledOrderReturnsCancelRejected) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 100, 10));
    book.submit(make_order(2, Side::Buy, 100, 10), sink);

    sink.clear();
    book.cancel(OrderId{1}, sink); // already fully filled

    const auto rejected = sink.events_of_type<CancelRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].order_id, OrderId{1});
    EXPECT_FALSE(rejected[0].reason.empty());
}

TEST(CancelTest, CancelOneOrderKeepsOthersAtSamePriceLevel) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 100, 5));
    book.add(make_order(2, Side::Sell, 100, 7));
    book.add(make_order(3, Side::Sell, 100, 3));

    book.cancel(OrderId{2}, sink);

    EXPECT_EQ(book.depth_at(Side::Sell, Price{100}), Quantity{8});
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), Price{100});

    // Remaining orders should still match in FIFO order (1 then 3)
    InMemoryEventSink match_sink;
    book.submit(make_order(10, Side::Buy, 100, 6), match_sink);
    const auto fills = match_sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 4u);
    EXPECT_EQ(fills[0], (Fill{OrderId{10}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{1}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[2], (Fill{OrderId{10}, Price{100}, Quantity{1}}));
    EXPECT_EQ(fills[3], (Fill{OrderId{3}, Price{100}, Quantity{1}}));
}

TEST(CancelTest, CancelRemovesEmptyPriceLevelAndUpdatesBest) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Buy, 100, 10));
    book.add(make_order(2, Side::Buy, 99, 10));

    book.cancel(OrderId{1}, sink);

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{99});
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{0});
}
