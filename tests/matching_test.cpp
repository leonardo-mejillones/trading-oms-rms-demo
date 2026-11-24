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

TEST(MatchingTest, NoMatchRestsGtcOrder) {
    OrderBook book;
    InMemoryEventSink sink;

    book.submit(make_order(1, Side::Buy, 100, 10), sink);

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{100});
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{10});
    EXPECT_TRUE(sink.events_of_type<Fill>().empty());
    ASSERT_EQ(sink.events_of_type<OrderAccepted>().size(), 1u);
    EXPECT_TRUE(sink.events_of_type<OrderCanceled>().empty());
}

TEST(MatchingTest, NoMatchWhenPriceDoesNotCross) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 105, 10));
    book.submit(make_order(2, Side::Buy, 100, 10), sink);

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), Price{105});
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{100});
    EXPECT_EQ(book.depth_at(Side::Sell, Price{105}), Quantity{10});
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{10});
    EXPECT_TRUE(sink.events_of_type<Fill>().empty());
}

TEST(MatchingTest, ExactMatch) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 100, 10));
    book.submit(make_order(2, Side::Buy, 100, 10), sink);

    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.depth_at(Side::Sell, Price{100}), Quantity{0});

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].order_id, OrderId{2});
    EXPECT_EQ(fills[0].price, Price{100});
    EXPECT_EQ(fills[0].quantity, Quantity{10});
    EXPECT_EQ(fills[1].order_id, OrderId{1});
    EXPECT_EQ(fills[1].price, Price{100});
    EXPECT_EQ(fills[1].quantity, Quantity{10});
}

TEST(MatchingTest, MultiLevelSweep) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 101, 5));
    book.add(make_order(2, Side::Sell, 102, 5));
    book.add(make_order(3, Side::Sell, 103, 5));

    book.submit(make_order(10, Side::Buy, 103, 12), sink);

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 6u);

    // Level 101: 5 filled
    EXPECT_EQ(fills[0], (Fill{OrderId{10}, Price{101}, Quantity{5}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{1}, Price{101}, Quantity{5}}));
    // Level 102: 5 filled
    EXPECT_EQ(fills[2], (Fill{OrderId{10}, Price{102}, Quantity{5}}));
    EXPECT_EQ(fills[3], (Fill{OrderId{2}, Price{102}, Quantity{5}}));
    // Level 103: 2 filled, 3 remaining
    EXPECT_EQ(fills[4], (Fill{OrderId{10}, Price{103}, Quantity{2}}));
    EXPECT_EQ(fills[5], (Fill{OrderId{3}, Price{103}, Quantity{2}}));

    EXPECT_FALSE(book.best_bid().has_value());
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), Price{103});
    EXPECT_EQ(book.depth_at(Side::Sell, Price{103}), Quantity{3});
    EXPECT_EQ(book.depth_at(Side::Sell, Price{101}), Quantity{0});
    EXPECT_EQ(book.depth_at(Side::Sell, Price{102}), Quantity{0});
}

TEST(MatchingTest, PartialFillRestsRemainingForGtc) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 100, 5));
    book.submit(make_order(2, Side::Buy, 100, 10), sink);

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0], (Fill{OrderId{2}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{1}, Price{100}, Quantity{5}}));

    EXPECT_FALSE(book.best_ask().has_value());
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{100});
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{5});
    EXPECT_TRUE(sink.events_of_type<OrderCanceled>().empty());
}

TEST(MatchingTest, PartialFillCancelsRemainingForIoc) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 100, 5));
    book.submit(make_order(2, Side::Buy, 100, 10, TimeInForce::IOC), sink);

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0], (Fill{OrderId{2}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{1}, Price{100}, Quantity{5}}));

    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{0});

    const auto canceled = sink.events_of_type<OrderCanceled>();
    ASSERT_EQ(canceled.size(), 1u);
    EXPECT_EQ(canceled[0].order_id, OrderId{2});
}

TEST(MatchingTest, ExecutesAtRestingPrice) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 100, 10));
    book.submit(make_order(2, Side::Buy, 105, 10), sink); // crosses at better price for buyer

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].price, Price{100});
    EXPECT_EQ(fills[1].price, Price{100});
}

TEST(MatchingTest, RespectsFifoWithinPriceLevel) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_order(1, Side::Sell, 100, 5));
    book.add(make_order(2, Side::Sell, 100, 5));

    book.submit(make_order(10, Side::Buy, 100, 7), sink);

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 4u);
    EXPECT_EQ(fills[0], (Fill{OrderId{10}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{1}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[2], (Fill{OrderId{10}, Price{100}, Quantity{2}}));
    EXPECT_EQ(fills[3], (Fill{OrderId{2}, Price{100}, Quantity{2}}));

    EXPECT_EQ(book.depth_at(Side::Sell, Price{100}), Quantity{3});
}
