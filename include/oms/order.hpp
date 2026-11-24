#pragma once

#include "oms/types.hpp"

namespace oms {

// Immutable domain model for an order accepted by the OMS.
// Construction validates basic invariants (non-zero IDs, positive quantity,
// and positive price for limit orders).
class Order {
public:
    Order(OrderId id,
          AccountId account,
          Side side,
          Price price,
          Quantity quantity,
          OrderType type = OrderType::Limit,
          TimeInForce tif = TimeInForce::GTC);

    [[nodiscard]] OrderId id() const noexcept { return id_; }
    [[nodiscard]] AccountId account() const noexcept { return account_; }
    [[nodiscard]] Side side() const noexcept { return side_; }
    [[nodiscard]] Price price() const noexcept { return price_; }
    [[nodiscard]] Quantity quantity() const noexcept { return quantity_; }
    [[nodiscard]] OrderType type() const noexcept { return type_; }
    [[nodiscard]] TimeInForce time_in_force() const noexcept { return tif_; }

    bool operator==(const Order&) const noexcept = default;

    friend std::ostream& operator<<(std::ostream& os, const Order& order);

private:
    OrderId id_;
    AccountId account_;
    Side side_;
    Price price_;
    Quantity quantity_;
    OrderType type_;
    TimeInForce tif_;
};

} // namespace oms
