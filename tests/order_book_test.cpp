#include <gtest/gtest.h>

#include "oms/order_book.hpp"

using namespace oms;

namespace {

Order make_order(uint64_t id, Side side, int64_t price_ticks, uint64_t qty) {
    return Order(OrderId{id}, AccountId{1}, side, Price{price_ticks}, Quantity{qty});
}

} // namespace

TEST(OrderBookTest, EmptyBookHasNoBestBidOrAsk) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{0});
    EXPECT_EQ(book.depth_at(Side::Sell, Price{100}), Quantity{0});
}

TEST(OrderBookTest, AddRestingBuyOrderUpdatesBestBidAndDepth) {
    OrderBook book;
    book.add(make_order(1, Side::Buy, 100, 10));

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{100});
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{10});
}

TEST(OrderBookTest, AddRestingSellOrderUpdatesBestAskAndDepth) {
    OrderBook book;
    book.add(make_order(1, Side::Sell, 105, 7));

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), Price{105});
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.depth_at(Side::Sell, Price{105}), Quantity{7});
}

TEST(OrderBookTest, BestBidIsHighestBuyPrice) {
    OrderBook book;
    book.add(make_order(1, Side::Buy, 100, 10));
    book.add(make_order(2, Side::Buy, 102, 5));
    book.add(make_order(3, Side::Buy, 101, 7));

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{102});
}

TEST(OrderBookTest, BestAskIsLowestSellPrice) {
    OrderBook book;
    book.add(make_order(1, Side::Sell, 110, 10));
    book.add(make_order(2, Side::Sell, 105, 5));
    book.add(make_order(3, Side::Sell, 108, 7));

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), Price{105});
}

TEST(OrderBookTest, DepthAggregatesQuantityAtSamePriceLevel) {
    OrderBook book;
    book.add(make_order(1, Side::Buy, 100, 10));
    book.add(make_order(2, Side::Buy, 100, 15));
    book.add(make_order(3, Side::Buy, 99, 20));

    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{25});
    EXPECT_EQ(book.depth_at(Side::Buy, Price{99}), Quantity{20});
    EXPECT_EQ(book.depth_at(Side::Buy, Price{101}), Quantity{0});
}

TEST(OrderBookTest, DepthIsTrackedPerSide) {
    OrderBook book;
    book.add(make_order(1, Side::Buy, 100, 10));
    book.add(make_order(2, Side::Sell, 100, 5));

    EXPECT_EQ(book.depth_at(Side::Buy, Price{100}), Quantity{10});
    EXPECT_EQ(book.depth_at(Side::Sell, Price{100}), Quantity{5});
}
