#include "oms/order.hpp"

#include <stdexcept>

namespace oms {

Order::Order(OrderId id,
             AccountId account,
             Side side,
             Price price,
             Quantity quantity,
             OrderType type,
             TimeInForce tif)
    : id_(id),
      account_(account),
      side_(side),
      price_(price),
      quantity_(quantity),
      type_(type),
      tif_(tif) {
    // Fail fast on invalid domain input to keep downstream components simple.
    if (id_.value == 0) {
        throw std::invalid_argument("Order id must be non-zero");
    }
    if (account_.value == 0) {
        throw std::invalid_argument("Account id must be non-zero");
    }
    if (quantity_.value == 0) {
        throw std::invalid_argument("Order quantity must be positive");
    }
    if (type_ == OrderType::Limit && price_.ticks <= 0) {
        throw std::invalid_argument("Limit order price must be positive");
    }
}

std::ostream& operator<<(std::ostream& os, const Order& order) {
    return os << "Order{id=" << order.id_
              << ", account=" << order.account_
              << ", side=" << order.side_
              << ", price=" << order.price_
              << ", qty=" << order.quantity_
              << ", type=" << order.type_
              << ", tif=" << order.tif_
              << "}";
}

} // namespace oms
