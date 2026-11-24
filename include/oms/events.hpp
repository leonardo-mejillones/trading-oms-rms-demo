#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "oms/types.hpp"

namespace oms {

struct OrderAccepted {
    OrderId order_id{};

    constexpr bool operator==(const OrderAccepted&) const noexcept = default;
};

struct OrderRejected {
    OrderId order_id{};
    std::string reason;

    bool operator==(const OrderRejected&) const = default;
};

struct Fill {
    OrderId order_id{};
    Price price{};
    Quantity quantity{};

    constexpr bool operator==(const Fill&) const noexcept = default;
};

struct OrderCanceled {
    OrderId order_id{};

    constexpr bool operator==(const OrderCanceled&) const noexcept = default;
};

struct CancelRejected {
    OrderId order_id{};
    std::string reason;

    bool operator==(const CancelRejected&) const = default;
};

using Event = std::variant<OrderAccepted, OrderRejected, Fill, OrderCanceled, CancelRejected>;

} // namespace oms
