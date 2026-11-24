#include "oms/order_book.hpp"

namespace oms {

void OrderBook::add(const Order& order) {
    if (order.side() == Side::Buy) {
        bids_[order.price()].push_back(order);
    } else {
        asks_[order.price()].push_back(order);
    }
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

Quantity OrderBook::depth_at(Side side, Price price) const {
    if (side == Side::Buy) {
        const auto it = bids_.find(price);
        if (it == bids_.end()) {
            return Quantity{0};
        }
        Quantity total{0};
        for (const auto& order : it->second) {
            total += order.quantity();
        }
        return total;
    }

    const auto it = asks_.find(price);
    if (it == asks_.end()) {
        return Quantity{0};
    }
    Quantity total{0};
    for (const auto& order : it->second) {
        total += order.quantity();
    }
    return total;
}

} // namespace oms
