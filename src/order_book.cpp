#include "oms/order_book.hpp"

#include "oms/event_sink.hpp"

namespace oms {

namespace {

Quantity min_qty(Quantity a, Quantity b) {
    return a < b ? a : b;
}

Order with_quantity(const Order& order, Quantity quantity) {
    return Order(order.id(), order.account(), order.side(), order.price(), quantity, order.type(),
                 order.time_in_force());
}

} // namespace

void OrderBook::add(const Order& order) {
    if (order.side() == Side::Buy) {
        bids_[order.price()].push_back(order);
    } else {
        asks_[order.price()].push_back(order);
    }
}

void OrderBook::submit(const Order& order, EventSink& sink) {
    sink.publish(OrderAccepted{order.id()});

    Quantity remaining = order.quantity();

    if (order.side() == Side::Buy) {
        while (remaining.value > 0 && !asks_.empty()) {
            auto best_ask_it = asks_.begin();
            if (best_ask_it->first > order.price()) {
                break;
            }

            auto& level = best_ask_it->second;
            while (remaining.value > 0 && !level.empty()) {
                Order& resting = level.front();
                const Quantity fill_qty = min_qty(remaining, resting.quantity());
                const Price fill_price = resting.price();

                sink.publish(Fill{order.id(), fill_price, fill_qty});
                sink.publish(Fill{resting.id(), fill_price, fill_qty});

                remaining = remaining - fill_qty;
                if (resting.quantity() == fill_qty) {
                    level.pop_front();
                } else {
                    resting = with_quantity(resting, resting.quantity() - fill_qty);
                }
            }

            if (level.empty()) {
                asks_.erase(best_ask_it);
            }
        }
    } else {
        while (remaining.value > 0 && !bids_.empty()) {
            auto best_bid_it = bids_.begin();
            if (best_bid_it->first < order.price()) {
                break;
            }

            auto& level = best_bid_it->second;
            while (remaining.value > 0 && !level.empty()) {
                Order& resting = level.front();
                const Quantity fill_qty = min_qty(remaining, resting.quantity());
                const Price fill_price = resting.price();

                sink.publish(Fill{order.id(), fill_price, fill_qty});
                sink.publish(Fill{resting.id(), fill_price, fill_qty});

                remaining = remaining - fill_qty;
                if (resting.quantity() == fill_qty) {
                    level.pop_front();
                } else {
                    resting = with_quantity(resting, resting.quantity() - fill_qty);
                }
            }

            if (level.empty()) {
                bids_.erase(best_bid_it);
            }
        }
    }

    if (remaining.value > 0) {
        if (order.time_in_force() == TimeInForce::IOC) {
            sink.publish(OrderCanceled{order.id()});
        } else {
            add(with_quantity(order, remaining));
        }
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
