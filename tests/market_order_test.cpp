#include <gtest/gtest.h>

#include "oms/order_book.hpp"
#include "oms/event_sink.hpp"

using namespace oms;

namespace {

Order make_limit(uint64_t id, Side side, int64_t price_ticks, uint64_t qty,
                 TimeInForce tif = TimeInForce::GTC) {
    return Order(OrderId{id}, AccountId{1}, side, Price{price_ticks}, Quantity{qty}, OrderType::Limit, tif);
}

Order make_market(uint64_t id, Side side, uint64_t qty) {
    return Order(OrderId{id}, AccountId{1}, side, Price{0}, Quantity{qty}, OrderType::Market, TimeInForce::IOC);
}

} // namespace

TEST(MarketOrderTest, BuyOnEmptyBookIsCanceled) {
    OrderBook book;
    InMemoryEventSink sink;

    book.submit(make_market(1, Side::Buy, 10), sink);

    EXPECT_TRUE(sink.events_of_type<Fill>().empty());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());

    const auto canceled = sink.events_of_type<OrderCanceled>();
    ASSERT_EQ(canceled.size(), 1u);
    EXPECT_EQ(canceled[0].order_id, OrderId{1});
}

TEST(MarketOrderTest, SellOnEmptyBookIsCanceled) {
    OrderBook book;
    InMemoryEventSink sink;

    book.submit(make_market(1, Side::Sell, 10), sink);

    EXPECT_TRUE(sink.events_of_type<Fill>().empty());
    const auto canceled = sink.events_of_type<OrderCanceled>();
    ASSERT_EQ(canceled.size(), 1u);
    EXPECT_EQ(canceled[0].order_id, OrderId{1});
}

TEST(MarketOrderTest, PartialLiquidityFillsAndCancelsRemainder) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_limit(1, Side::Sell, 101, 4));
    book.add(make_limit(2, Side::Sell, 102, 3));

    book.submit(make_market(10, Side::Buy, 10), sink);

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 4u);
    EXPECT_EQ(fills[0], (Fill{OrderId{10}, Price{101}, Quantity{4}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{1}, Price{101}, Quantity{4}}));
    EXPECT_EQ(fills[2], (Fill{OrderId{10}, Price{102}, Quantity{3}}));
    EXPECT_EQ(fills[3], (Fill{OrderId{2}, Price{102}, Quantity{3}}));

    const auto canceled = sink.events_of_type<OrderCanceled>();
    ASSERT_EQ(canceled.size(), 1u);
    EXPECT_EQ(canceled[0].order_id, OrderId{10});

    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(MarketOrderTest, ExecutesImmediatelyAtBestAvailablePrices) {
    OrderBook book;
    InMemoryEventSink sink;

    // Worse price added first in insertion order but higher/lower price levels determine priority.
    book.add(make_limit(1, Side::Sell, 105, 5));
    book.add(make_limit(2, Side::Sell, 101, 5));
    book.add(make_limit(3, Side::Sell, 103, 5));

    book.submit(make_market(10, Side::Buy, 12), sink);

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 6u);
    EXPECT_EQ(fills[0], (Fill{OrderId{10}, Price{101}, Quantity{5}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{2}, Price{101}, Quantity{5}}));
    EXPECT_EQ(fills[2], (Fill{OrderId{10}, Price{103}, Quantity{5}}));
    EXPECT_EQ(fills[3], (Fill{OrderId{3}, Price{103}, Quantity{5}}));
    EXPECT_EQ(fills[4], (Fill{OrderId{10}, Price{105}, Quantity{2}}));
    EXPECT_EQ(fills[5], (Fill{OrderId{1}, Price{105}, Quantity{2}}));

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), Price{105});
    EXPECT_EQ(book.depth_at(Side::Sell, Price{105}), Quantity{3});
    EXPECT_TRUE(sink.events_of_type<OrderCanceled>().empty());
}
