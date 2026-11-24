#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "oms/order.hpp"

namespace oms {

enum class RejectReason : uint8_t {
    None = 0,
    InvalidQuantity,
    InvalidPrice,
    InvalidTickSize,
    InvalidLotSize,
    UnknownSymbol
};

constexpr std::string_view to_string(RejectReason reason) noexcept {
    switch (reason) {
        case RejectReason::None:            return "None";
        case RejectReason::InvalidQuantity: return "InvalidQuantity";
        case RejectReason::InvalidPrice:    return "InvalidPrice";
        case RejectReason::InvalidTickSize: return "InvalidTickSize";
        case RejectReason::InvalidLotSize:  return "InvalidLotSize";
        case RejectReason::UnknownSymbol:   return "UnknownSymbol";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, RejectReason reason) {
    return os << to_string(reason);
}

struct InstrumentSpec {
    std::string symbol;
    int64_t tick_size{1};   // in price ticks
    uint64_t lot_size{1};   // in quantity units
};

// Raw order request used for early OMS validation before constructing a domain Order.
struct OrderRequest {
    OrderId id{};
    AccountId account{};
    ClientOrderId client_order_id{};
    std::string symbol;
    Side side{Side::Buy};
    Price price{};
    Quantity quantity{};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
};

// Validates an order request against basic rules and instrument constraints.
// Returns RejectReason on failure, std::nullopt when the request is valid.
[[nodiscard]] std::optional<RejectReason> validate_order(
    const OrderRequest& request,
    const std::unordered_map<std::string, InstrumentSpec>& instruments);

// Convenience overload for already-constructed domain orders.
[[nodiscard]] std::optional<RejectReason> validate_order(
    const Order& order,
    const std::unordered_map<std::string, InstrumentSpec>& instruments);

} // namespace oms
