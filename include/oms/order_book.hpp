#pragma once

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>

#include "oms/order.hpp"

namespace oms {

class EventSink;

/// Limit order book with price-time priority.
///
/// Bids are ordered by descending price, asks by ascending price.
/// Each price level keeps a FIFO queue of resting orders.
/// An auxiliary index maps OrderId to its location for O(1) cancel lookup.
class OrderBook {
public:
    /// Adds an order as resting liquidity without attempting to match it.
    /// The order is appended to the back of its side/price queue (FIFO).
    void add(const Order& order);

    /// Submits an incoming order (Limit or Market) for matching.
    ///
    /// Matching follows price-time priority:
    /// - A buy matches asks from lowest price upward.
    /// - A sell matches bids from highest price downward.
    /// - Limit orders only cross prices within their limit; market orders cross any
    ///   available price on the opposite side.
    /// - Within the same price level, resting orders are matched FIFO.
    ///
    /// Trades execute at the resting order price. For each execution, Fill events
    /// are published for both the incoming and resting orders. If the incoming order
    /// is not fully filled, GTC limit remainder rests in the book; IOC and Market
    /// remainders are canceled (OrderCanceled event).
    void submit(const Order& order, EventSink& sink);

    /// Cancels a resting order by OrderId.
    ///
    /// Uses the order index for direct lookup (no full book scan). If the order is
    /// unknown or already filled, publishes CancelRejected with a reason.
    void cancel(OrderId order_id, EventSink& sink);

    /// Returns the highest bid price, or nullopt if there are no bids.
    [[nodiscard]] std::optional<Price> best_bid() const;

    /// Returns the lowest ask price, or nullopt if there are no asks.
    [[nodiscard]] std::optional<Price> best_ask() const;

    /// Returns the total resting quantity at a side/price level.
    [[nodiscard]] Quantity depth_at(Side side, Price price) const;

private:
    using PriceLevel = std::list<Order>;
    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;

    // Location metadata used by the OrderId index for O(1) cancel/remove.
    struct OrderLocation {
        Side side;
        Price price;
        PriceLevel::iterator it;
    };

    void index_order(Side side, Price price, PriceLevel::iterator it);
    void unindex_order(OrderId order_id);

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, OrderLocation> order_index_;
};

} // namespace oms
