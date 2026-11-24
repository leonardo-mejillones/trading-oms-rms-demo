#include <gtest/gtest.h>

#include "oms/order_book.hpp"
#include "oms/event_sink.hpp"

using namespace oms;

namespace {

Order make_limit(uint64_t id, Side side, int64_t price_ticks, uint64_t qty,
                 TimeInForce tif = TimeInForce::GTC) {
    return Order(OrderId{id}, AccountId{1}, side, Price{price_ticks}, Quantity{qty}, OrderType::Limit, tif);
}

} // namespace

TEST(ReplaceTest, DecreaseSizeSamePriceKeepsTimePriority) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_limit(1, Side::Sell, 100, 10));
    book.add(make_limit(2, Side::Sell, 100, 10));

    book.replace(OrderId{1}, Price{100}, Quantity{4}, sink);

    EXPECT_EQ(book.depth_at(Side::Sell, Price{100}), Quantity{14});
    EXPECT_TRUE(sink.events_of_type<CancelRejected>().empty());

    // Order 1 should keep its original FIFO position ahead of order 2.
    InMemoryEventSink match_sink;
    book.submit(make_limit(10, Side::Buy, 100, 6), match_sink);

    const auto fills = match_sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 4u);
    EXPECT_EQ(fills[0], (Fill{OrderId{10}, Price{100}, Quantity{4}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{1}, Price{100}, Quantity{4}}));
    EXPECT_EQ(fills[2], (Fill{OrderId{10}, Price{100}, Quantity{2}}));
    EXPECT_EQ(fills[3], (Fill{OrderId{2}, Price{100}, Quantity{2}}));
}

TEST(ReplaceTest, IncreaseSizeSamePriceLosesTimePriority) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_limit(1, Side::Sell, 100, 5));
    book.add(make_limit(2, Side::Sell, 100, 5));

    book.replace(OrderId{1}, Price{100}, Quantity{7}, sink); // loses priority

    EXPECT_EQ(book.depth_at(Side::Sell, Price{100}), Quantity{12});

    // Order 2 should now be ahead of order 1.
    InMemoryEventSink match_sink;
    book.submit(make_limit(10, Side::Buy, 100, 6), match_sink);

    const auto fills = match_sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 4u);
    EXPECT_EQ(fills[0], (Fill{OrderId{10}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[1], (Fill{OrderId{2}, Price{100}, Quantity{5}}));
    EXPECT_EQ(fills[2], (Fill{OrderId{10}, Price{100}, Quantity{1}}));
    EXPECT_EQ(fills[3], (Fill{OrderId{1}, Price{100}, Quantity{1}}));
}

TEST(ReplaceTest, PriceChangeLosesTimePriority) {
    OrderBook book;
    InMemoryEventSink sink;

    book.add(make_limit(1, Side::Buy, 100, 10));
    book.add(make_limit(2, Side::Buy, 100, 10));

    book.replace(OrderId{1}, Price{99}, Quantity{10}, sink); // price change loses priority

    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{10});
    EXPECT_EQ(book.depth_at(Side::Buy, Price{99}), Quantity{10});
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{100});
}

TEST(ReplaceTest, UnknownOrFilledOrderIsRejected) {
    OrderBook book;
    InMemoryEventSink sink;

    book.replace(OrderId{999}, Price{100}, Quantity{1}, sink);
    auto rejected = sink.events_of_type<CancelRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].order_id, OrderId{999});

    sink.clear();
    book.add(make_limit(1, Side::Sell, 100, 5));
    book.submit(make_limit(2, Side::Buy, 100, 5), sink); // fills order 1
    sink.clear();

    book.replace(OrderId{1}, Price{100}, Quantity{1}, sink);
    rejected = sink.events_of_type<CancelRejected>();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].order_id, OrderId{1});
}
