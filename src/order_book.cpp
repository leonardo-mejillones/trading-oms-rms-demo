#include "oms/order_book.hpp"

#include "oms/event_sink.hpp"

namespace oms {

namespace {

Quantity min_qty(Quantity a, Quantity b) {
    return a < b ? a : b;
}

// Creates a copy of an order with a new remaining quantity.
// Used when a resting or incoming order is partially filled.
Order with_quantity(const Order& order, Quantity quantity) {
    return Order(order.id(), order.account(), order.side(), order.price(), quantity, order.type(),
                 order.time_in_force());
}

} // namespace

void OrderBook::index_order(Side side, Price price, PriceLevel::iterator it) {
    order_index_[it->id()] = OrderLocation{side, price, it};
}

void OrderBook::unindex_order(OrderId order_id) {
    order_index_.erase(order_id);
}

void OrderBook::add(const Order& order) {
    if (order.side() == Side::Buy) {
        auto& level = bids_[order.price()];
        auto it = level.insert(level.end(), order);
        index_order(order.side(), order.price(), it);
    } else {
        auto& level = asks_[order.price()];
        auto it = level.insert(level.end(), order);
        index_order(order.side(), order.price(), it);
    }
}

void OrderBook::submit(const Order& order, EventSink& sink) {
    // The order is accepted for processing before matching/resting decisions.
    sink.publish(OrderAccepted{order.id()});

    Quantity remaining = order.quantity();

    if (order.side() == Side::Buy) {
        // Buy orders cross the ask side, starting from the lowest ask price.
        // Market orders have no price limit; limit orders stop when ask price is above limit.
        while (remaining.value > 0 && !asks_.empty()) {
            auto best_ask_it = asks_.begin();
            if (order.type() == OrderType::Limit && best_ask_it->first > order.price()) {
                // Best ask is above buy limit: no more crossing prices.
                break;
            }

            auto& level = best_ask_it->second;
            while (remaining.value > 0 && !level.empty()) {
                Order& resting = level.front();
                const Quantity fill_qty = min_qty(remaining, resting.quantity());
                const Price fill_price = resting.price(); // Execute at resting price.

                // Emit fills for both sides of the execution.
                sink.publish(Fill{order.id(), fill_price, fill_qty});
                sink.publish(Fill{resting.id(), fill_price, fill_qty});

                remaining = remaining - fill_qty;
                if (resting.quantity() == fill_qty) {
                    // Fully filled resting order: remove from index and FIFO queue.
                    unindex_order(resting.id());
                    level.pop_front();
                } else {
                    // Partially filled resting order: keep same list node/iterator,
                    // but replace its contents with reduced remaining quantity.
                    resting = with_quantity(resting, resting.quantity() - fill_qty);
                }
            }

            if (level.empty()) {
                asks_.erase(best_ask_it);
            }
        }
    } else {
        // Sell orders cross the bid side, starting from the highest bid price.
        // Market orders have no price limit; limit orders stop when bid price is below limit.
        while (remaining.value > 0 && !bids_.empty()) {
            auto best_bid_it = bids_.begin();
            if (order.type() == OrderType::Limit && best_bid_it->first < order.price()) {
                // Best bid is below sell limit: no more crossing prices.
                break;
            }

            auto& level = best_bid_it->second;
            while (remaining.value > 0 && !level.empty()) {
                Order& resting = level.front();
                const Quantity fill_qty = min_qty(remaining, resting.quantity());
                const Price fill_price = resting.price(); // Execute at resting price.

                // Emit fills for both sides of the execution.
                sink.publish(Fill{order.id(), fill_price, fill_qty});
                sink.publish(Fill{resting.id(), fill_price, fill_qty});

                remaining = remaining - fill_qty;
                if (resting.quantity() == fill_qty) {
                    // Fully filled resting order: remove from index and FIFO queue.
                    unindex_order(resting.id());
                    level.pop_front();
                } else {
                    // Partially filled resting order: keep same list node/iterator,
                    // but replace its contents with reduced remaining quantity.
                    resting = with_quantity(resting, resting.quantity() - fill_qty);
                }
            }

            if (level.empty()) {
                bids_.erase(best_bid_it);
            }
        }
    }

    if (remaining.value > 0) {
        if (order.type() == OrderType::Market || order.time_in_force() == TimeInForce::IOC) {
            // Market and IOC orders never leave residual resting liquidity.
            sink.publish(OrderCanceled{order.id()});
        } else {
            // GTC remainder becomes resting liquidity at its limit price.
            add(with_quantity(order, remaining));
        }
    }
}

void OrderBook::cancel(OrderId order_id, EventSink& sink) {
    // Direct index lookup avoids scanning all price levels/orders.
    const auto index_it = order_index_.find(order_id);
    if (index_it == order_index_.end()) {
        sink.publish(CancelRejected{order_id, "unknown or already filled order"});
        return;
    }

    const OrderLocation location = index_it->second;
    if (location.side == Side::Buy) {
        auto level_it = bids_.find(location.price);
        if (level_it != bids_.end()) {
            level_it->second.erase(location.it);
            if (level_it->second.empty()) {
                bids_.erase(level_it); // Keep best-bid/level map clean.
            }
        }
    } else {
        auto level_it = asks_.find(location.price);
        if (level_it != asks_.end()) {
            level_it->second.erase(location.it);
            if (level_it->second.empty()) {
                asks_.erase(level_it); // Keep best-ask/level map clean.
            }
        }
    }

    unindex_order(order_id);
    sink.publish(OrderCanceled{order_id});
}

void OrderBook::replace(OrderId order_id, Price new_price, Quantity new_quantity, EventSink& sink) {
    const auto index_it = order_index_.find(order_id);
    if (index_it == order_index_.end()) {
        sink.publish(CancelRejected{order_id, "unknown or already filled order"});
        return;
    }

    const OrderLocation location = index_it->second;
    const Order& current = *location.it;

    if (new_quantity.value == 0) {
        sink.publish(CancelRejected{order_id, "replace quantity must be positive"});
        return;
    }

    // Size decrease at the same price keeps time priority: update in place.
    if (new_price == current.price() && new_quantity < current.quantity()) {
        Order& resting = *location.it;
        resting = with_quantity(resting, new_quantity);
        return;
    }

    // No-op replace: keep current priority and state.
    if (new_price == current.price() && new_quantity == current.quantity()) {
        return;
    }

    // Price change or size increase loses time priority: remove old and re-queue at back.
    // Validate/construct replacement before mutating the book.
    std::optional<Order> replacement;
    try {
        replacement.emplace(current.id(), current.account(), current.side(), new_price, new_quantity,
                            current.type(), current.time_in_force());
    } catch (const std::invalid_argument&) {
        sink.publish(CancelRejected{order_id, "invalid replacement order"});
        return;
    }

    if (location.side == Side::Buy) {
        auto level_it = bids_.find(location.price);
        if (level_it != bids_.end()) {
            level_it->second.erase(location.it);
            if (level_it->second.empty()) {
                bids_.erase(level_it);
            }
        }
    } else {
        auto level_it = asks_.find(location.price);
        if (level_it != asks_.end()) {
            level_it->second.erase(location.it);
            if (level_it->second.empty()) {
                asks_.erase(level_it);
            }
        }
    }

    unindex_order(order_id);
    // Re-submit so crossing replacements are matched immediately.
    submit(*replacement, sink);
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
        for (const auto& resting : it->second) {
            total += resting.quantity();
        }
        return total;
    }

    const auto it = asks_.find(price);
    if (it == asks_.end()) {
        return Quantity{0};
    }
    Quantity total{0};
    for (const auto& resting : it->second) {
        total += resting.quantity();
    }
    return total;
}

} // namespace oms
