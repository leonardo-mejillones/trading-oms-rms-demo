#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "oms/types.hpp"

namespace oms {

// Emitted when an incoming order is accepted for processing.
struct OrderAccepted {
    OrderId order_id{};

    constexpr bool operator==(const OrderAccepted&) const noexcept = default;
};

// Emitted when an order is rejected (e.g., validation or risk checks in future stories).
struct OrderRejected {
    OrderId order_id{};
    std::string reason;

    bool operator==(const OrderRejected&) const = default;
};

// Represents an execution/fill for one side of a trade.
// account/symbol/side are enriched metadata for risk/position tracking.
// Equality compares core execution fields to keep existing tests stable.
struct Fill {
    OrderId order_id{};
    Price price{};
    Quantity quantity{};
    AccountId account{};
    std::string symbol;
    Side side{Side::Buy};

    Fill() = default;
    Fill(OrderId oid, Price p, Quantity q, AccountId acc = {}, std::string sym = {}, Side s = Side::Buy)
        : order_id(oid), price(p), quantity(q), account(acc), symbol(std::move(sym)), side(s) {}

    bool operator==(const Fill& other) const noexcept {
        return order_id == other.order_id && price == other.price && quantity == other.quantity;
    }
};

// Emitted when an order is canceled (user cancel or IOC remainder).
struct OrderCanceled {
    OrderId order_id{};

    constexpr bool operator==(const OrderCanceled&) const noexcept = default;
};

// Emitted when a cancel request cannot be applied.
struct CancelRejected {
    OrderId order_id{};
    std::string reason;

    bool operator==(const CancelRejected&) const = default;
};

// Type-safe event container for all OMS domain events.
using Event = std::variant<OrderAccepted, OrderRejected, Fill, OrderCanceled, CancelRejected>;

} // namespace oms
