#include <gtest/gtest.h>

#include "oms/validation.hpp"

using namespace oms;

namespace {

std::unordered_map<std::string, InstrumentSpec> make_instruments() {
    return {
        {"AAPL", InstrumentSpec{"AAPL", 5, 10}},  // tick size 5, lot size 10
    };
}

OrderRequest make_request(std::string symbol = "AAPL", int64_t price = 100, uint64_t qty = 10) {
    OrderRequest req;
    req.id = OrderId{1};
    req.account = AccountId{1};
    req.client_order_id = ClientOrderId{1};
    req.symbol = std::move(symbol);
    req.side = Side::Buy;
    req.price = Price{price};
    req.quantity = Quantity{qty};
    req.type = OrderType::Limit;
    req.tif = TimeInForce::GTC;
    return req;
}

} // namespace

TEST(ValidationTest, ValidOrderPasses) {
    const auto instruments = make_instruments();
    const auto reason = validate_order(make_request("AAPL", 100, 10), instruments);
    EXPECT_FALSE(reason.has_value());
}

TEST(ValidationTest, PositiveQuantityRequired) {
    const auto instruments = make_instruments();
    auto req = make_request("AAPL", 100, 0);
    const auto reason = validate_order(req, instruments);
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(*reason, RejectReason::InvalidQuantity);
}

TEST(ValidationTest, PositivePriceRequiredForLimit) {
    const auto instruments = make_instruments();
    auto req = make_request("AAPL", 0, 10);
    const auto reason = validate_order(req, instruments);
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(*reason, RejectReason::InvalidPrice);
}

TEST(ValidationTest, TickSizeEnforced) {
    const auto instruments = make_instruments();
    auto req = make_request("AAPL", 103, 10); // not multiple of tick size 5
    const auto reason = validate_order(req, instruments);
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(*reason, RejectReason::InvalidTickSize);
}

TEST(ValidationTest, LotSizeEnforced) {
    const auto instruments = make_instruments();
    auto req = make_request("AAPL", 100, 15); // not multiple of lot size 10
    const auto reason = validate_order(req, instruments);
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(*reason, RejectReason::InvalidLotSize);
}

TEST(ValidationTest, KnownSymbolRequired) {
    const auto instruments = make_instruments();
    auto req = make_request("MSFT", 100, 10);
    const auto reason = validate_order(req, instruments);
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(*reason, RejectReason::UnknownSymbol);
}

TEST(ValidationTest, EachFailureMapsToDistinctReason) {
    const auto instruments = make_instruments();

    const auto qty = validate_order(make_request("AAPL", 100, 0), instruments);
    const auto price = validate_order(make_request("AAPL", 0, 10), instruments);
    const auto tick = validate_order(make_request("AAPL", 103, 10), instruments);
    const auto lot = validate_order(make_request("AAPL", 100, 15), instruments);
    const auto symbol = validate_order(make_request("MSFT", 100, 10), instruments);

    ASSERT_TRUE(qty.has_value());
    ASSERT_TRUE(price.has_value());
    ASSERT_TRUE(tick.has_value());
    ASSERT_TRUE(lot.has_value());
    ASSERT_TRUE(symbol.has_value());

    EXPECT_NE(*qty, *price);
    EXPECT_NE(*qty, *tick);
    EXPECT_NE(*qty, *lot);
    EXPECT_NE(*qty, *symbol);
    EXPECT_NE(*price, *tick);
    EXPECT_NE(*price, *lot);
    EXPECT_NE(*price, *symbol);
    EXPECT_NE(*tick, *lot);
    EXPECT_NE(*tick, *symbol);
    EXPECT_NE(*lot, *symbol);
}
