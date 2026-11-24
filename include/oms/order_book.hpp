#pragma once

#include <deque>
#include <functional>
#include <map>
#include <optional>

#include "oms/order.hpp"

namespace oms {

class EventSink;

class OrderBook {
public:
    void add(const Order& order);
    void submit(const Order& order, EventSink& sink);

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] Quantity depth_at(Side side, Price price) const;

private:
    using PriceLevel = std::deque<Order>;
    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;

    BidLevels bids_;
    AskLevels asks_;
};

} // namespace oms
