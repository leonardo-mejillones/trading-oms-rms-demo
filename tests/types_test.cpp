#include <gtest/gtest.h>
#include <type_traits>
#include <unordered_set>

#include "oms/order.hpp"
#include "oms/types.hpp"

using namespace oms;

// Compile-time checks: ensure float/double cannot construct Price or Quantity
static_assert(!std::is_constructible_v<Price, double>);
static_assert(!std::is_constructible_v<Price, float>);
static_assert(!std::is_constructible_v<Quantity, double>);
static_assert(!std::is_constructible_v<Quantity, float>);

// Ensure strongly-typed wrappers are not implicitly interchangeable
static_assert(!std::is_convertible_v<OrderId, AccountId>);
static_assert(!std::is_convertible_v<Price, Quantity>);
static_assert(!std::is_convertible_v<Price, int64_t>);
static_assert(!std::is_convertible_v<Quantity, uint64_t>);

TEST(TypesTest, PriceArithmeticAndComparisons) {
    Price p1{100};
    Price p2{50};

    EXPECT_EQ(p1 + p2, Price(150));
    EXPECT_EQ(p1 - p2, Price(50));
    EXPECT_EQ(p1 * 2, Price(200));
    EXPECT_EQ(2 * p1, Price(200));
    EXPECT_EQ(p1 / 2, Price(50));
    EXPECT_THROW((void)(p1 / 0), std::invalid_argument);

    p1 += Price{25};
    EXPECT_EQ(p1, Price(125));
    p1 -= Price{25};
    EXPECT_EQ(p1, Price(100));

    EXPECT_LT(p2, p1);
    EXPECT_LE(p2, p1);
    EXPECT_GT(p1, p2);
    EXPECT_GE(p1, p2);
    EXPECT_NE(p1, p2);
}

TEST(TypesTest, QuantityArithmeticAndComparisons) {
    Quantity q1{100};
    Quantity q2{40};

    EXPECT_EQ(q1 + q2, Quantity(140));
    EXPECT_EQ(q1 - q2, Quantity(60));
    EXPECT_EQ(q1 * 3, Quantity(300));
    EXPECT_EQ(3 * q1, Quantity(300));

    EXPECT_THROW((void)(q2 - q1), std::invalid_argument);

    q1 += Quantity{10};
    EXPECT_EQ(q1, Quantity(110));
    q1 -= Quantity{10};
    EXPECT_EQ(q1, Quantity(100));
    EXPECT_THROW(q2 -= Quantity{100}, std::invalid_argument);

    EXPECT_LT(q2, q1);
    EXPECT_GT(q1, q2);
}

TEST(TypesTest, HashingInUnorderedSet) {
    std::unordered_set<OrderId> order_ids;
    order_ids.insert(OrderId{1});
    order_ids.insert(OrderId{2});
    EXPECT_EQ(order_ids.count(OrderId{1}), 1u);
    EXPECT_EQ(order_ids.count(OrderId{99}), 0u);

    std::unordered_set<AccountId> account_ids;
    account_ids.insert(AccountId{10});
    EXPECT_EQ(account_ids.count(AccountId{10}), 1u);

    std::unordered_set<Price> prices;
    prices.insert(Price{500});
    EXPECT_EQ(prices.count(Price{500}), 1u);

    std::unordered_set<Quantity> quantities;
    quantities.insert(Quantity{25});
    EXPECT_EQ(quantities.count(Quantity{25}), 1u);
}

TEST(TypesTest, EnumStrings) {
    EXPECT_EQ(to_string(Side::Buy), "Buy");
    EXPECT_EQ(to_string(Side::Sell), "Sell");
    EXPECT_EQ(to_string(OrderType::Limit), "Limit");
    EXPECT_EQ(to_string(TimeInForce::GTC), "GTC");
    EXPECT_EQ(to_string(TimeInForce::IOC), "IOC");
}

TEST(OrderTest, ValidConstruction) {
    Order order(OrderId{1}, AccountId{42}, Side::Buy, Price{15000}, Quantity{100});

    EXPECT_EQ(order.id(), OrderId{1});
    EXPECT_EQ(order.account(), AccountId{42});
    EXPECT_EQ(order.side(), Side::Buy);
    EXPECT_EQ(order.price(), Price{15000});
    EXPECT_EQ(order.quantity(), Quantity{100});
    EXPECT_EQ(order.type(), OrderType::Limit);
    EXPECT_EQ(order.time_in_force(), TimeInForce::GTC);
}

TEST(OrderTest, ValidConstructionCustomTypeAndTif) {
    Order order(OrderId{2}, AccountId{10}, Side::Sell, Price{200}, Quantity{5},
                OrderType::Limit, TimeInForce::IOC);

    EXPECT_EQ(order.side(), Side::Sell);
    EXPECT_EQ(order.time_in_force(), TimeInForce::IOC);
}

TEST(OrderTest, RejectsInvalidParameters) {
    // Zero OrderId
    EXPECT_THROW(
        Order(OrderId{0}, AccountId{1}, Side::Buy, Price{100}, Quantity{10}),
        std::invalid_argument);

    // Zero AccountId
    EXPECT_THROW(
        Order(OrderId{1}, AccountId{0}, Side::Buy, Price{100}, Quantity{10}),
        std::invalid_argument);

    // Zero Quantity
    EXPECT_THROW(
        Order(OrderId{1}, AccountId{1}, Side::Buy, Price{100}, Quantity{0}),
        std::invalid_argument);

    // Zero or Negative Price for Limit order
    EXPECT_THROW(
        Order(OrderId{1}, AccountId{1}, Side::Buy, Price{0}, Quantity{10}),
        std::invalid_argument);
    EXPECT_THROW(
        Order(OrderId{1}, AccountId{1}, Side::Buy, Price{-50}, Quantity{10}),
        std::invalid_argument);
}
