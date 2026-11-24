#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "oms/events.hpp"
#include "oms/risk.hpp"

namespace oms {

// Tracks net positions and open-order exposure per (account, symbol).
// Positions are updated from fills; exposure checks include open orders.
class PositionTracker {
public:
    struct Key {
        AccountId account{};
        std::string symbol;

        bool operator==(const Key&) const noexcept = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            const std::size_t h1 = std::hash<uint64_t>{}(k.account.value);
            const std::size_t h2 = std::hash<std::string>{}(k.symbol);
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
        }
    };

    void register_order(const Order& order) {
        if (order.symbol().empty()) {
            return;
        }
        OpenOrder& open = open_orders_[order.id()];
        if (open.active) {
            return; // already registered
        }
        open = OpenOrder{order.account(), order.symbol(), order.side(), order.quantity(), true};
        auto& exposure = exposure_[Key{order.account(), order.symbol()}];
        if (order.side() == Side::Buy) {
            exposure.open_buy += order.quantity().value;
        } else {
            exposure.open_sell += order.quantity().value;
        }
    }

    void apply(const Event& event) {
        if (const auto* fill = std::get_if<Fill>(&event)) {
            apply_fill(*fill);
        } else if (const auto* canceled = std::get_if<OrderCanceled>(&event)) {
            close_open_order(canceled->order_id);
        } else if (const auto* rejected = std::get_if<OrderRejected>(&event)) {
            close_open_order(rejected->order_id);
        }
    }

    [[nodiscard]] int64_t position(AccountId account, const std::string& symbol) const {
        const auto it = exposure_.find(Key{account, symbol});
        return it == exposure_.end() ? 0 : it->second.net_position;
    }

    [[nodiscard]] uint64_t open_buy(AccountId account, const std::string& symbol) const {
        const auto it = exposure_.find(Key{account, symbol});
        return it == exposure_.end() ? 0 : it->second.open_buy;
    }

    [[nodiscard]] uint64_t open_sell(AccountId account, const std::string& symbol) const {
        const auto it = exposure_.find(Key{account, symbol});
        return it == exposure_.end() ? 0 : it->second.open_sell;
    }

    [[nodiscard]] bool has_open_order(OrderId id) const {
        const auto it = open_orders_.find(id);
        return it != open_orders_.end() && it->second.active;
    }

    struct OpenOrderInfo {
        AccountId account{};
        std::string symbol;
        Side side{Side::Buy};
        Quantity remaining{0};
    };

    [[nodiscard]] std::optional<OpenOrderInfo> open_order(OrderId id) const {
        const auto it = open_orders_.find(id);
        if (it == open_orders_.end() || !it->second.active) {
            return std::nullopt;
        }
        return OpenOrderInfo{it->second.account, it->second.symbol, it->second.side, it->second.remaining};
    }

    void update_open_quantity(OrderId id, Quantity new_remaining) {
        auto it = open_orders_.find(id);
        if (it == open_orders_.end() || !it->second.active) {
            return;
        }
        OpenOrder& open = it->second;
        auto& exposure = exposure_[Key{open.account, open.symbol}];
        const uint64_t old_qty = open.remaining.value;
        const uint64_t new_qty = new_remaining.value;
        if (open.side == Side::Buy) {
            exposure.open_buy = exposure.open_buy - old_qty + new_qty;
        } else {
            exposure.open_sell = exposure.open_sell - old_qty + new_qty;
        }
        open.remaining = new_remaining;
        if (open.remaining.value == 0) {
            open.active = false;
        }
    }

    void close_open_order(OrderId order_id) {
        auto it = open_orders_.find(order_id);
        if (it == open_orders_.end() || !it->second.active) {
            return;
        }
        OpenOrder& open = it->second;
        auto& exposure = exposure_[Key{open.account, open.symbol}];
        if (open.side == Side::Buy) {
            exposure.open_buy = (exposure.open_buy >= open.remaining.value)
                                    ? exposure.open_buy - open.remaining.value
                                    : 0;
        } else {
            exposure.open_sell = (exposure.open_sell >= open.remaining.value)
                                     ? exposure.open_sell - open.remaining.value
                                     : 0;
        }
        open.remaining = Quantity{0};
        open.active = false;
    }

private:
    struct Exposure {
        int64_t net_position{0};
        uint64_t open_buy{0};
        uint64_t open_sell{0};
    };

    struct OpenOrder {
        AccountId account{};
        std::string symbol;
        Side side{Side::Buy};
        Quantity remaining{0};
        bool active{false};
    };

    void apply_fill(const Fill& fill) {
        // Update net position from fill.
        if (!fill.symbol.empty()) {
            auto& exposure = exposure_[Key{fill.account, fill.symbol}];
            if (fill.side == Side::Buy) {
                exposure.net_position += static_cast<int64_t>(fill.quantity.value);
            } else {
                exposure.net_position -= static_cast<int64_t>(fill.quantity.value);
            }
        }

        // Reduce open order remaining quantity.
        auto it = open_orders_.find(fill.order_id);
        if (it == open_orders_.end() || !it->second.active) {
            return;
        }
        OpenOrder& open = it->second;
        auto& exposure = exposure_[Key{open.account, open.symbol}];
        const uint64_t filled = fill.quantity.value;
        if (open.side == Side::Buy) {
            exposure.open_buy = (exposure.open_buy >= filled) ? exposure.open_buy - filled : 0;
        } else {
            exposure.open_sell = (exposure.open_sell >= filled) ? exposure.open_sell - filled : 0;
        }
        if (open.remaining.value > filled) {
            open.remaining = Quantity{open.remaining.value - filled};
        } else {
            open.remaining = Quantity{0};
            open.active = false;
        }
    }

    std::unordered_map<Key, Exposure, KeyHash> exposure_;
    std::unordered_map<OrderId, OpenOrder> open_orders_;
};

// Pre-trade position limit check. Considers worst-case exposure including open orders.
class PositionLimitCheck : public RiskCheck {
public:
    PositionLimitCheck(PositionTracker& tracker, int64_t max_long, int64_t max_short)
        : tracker_(tracker), max_long_(max_long), max_short_(max_short) {}

    [[nodiscard]] RiskResult check(const Order& order) const override {
        if (order.symbol().empty()) {
            return RiskResult::pass();
        }

        const int64_t current = tracker_.position(order.account(), order.symbol());
        const uint64_t open_buy = tracker_.open_buy(order.account(), order.symbol());
        const uint64_t open_sell = tracker_.open_sell(order.account(), order.symbol());
        const uint64_t qty = order.quantity().value;

        const bool already_registered = tracker_.has_open_order(order.id());
        const uint64_t order_qty = already_registered ? 0 : qty;

        if (order.side() == Side::Buy) {
            // Worst case: this buy and all open buys fill, no sells fill.
            const int64_t projected = current + static_cast<int64_t>(open_buy) + static_cast<int64_t>(order_qty);
            if (projected > max_long_) {
                return RiskResult::reject("position limit exceeded");
            }
        } else {
            // Worst case: this sell and all open sells fill, no buys fill.
            const int64_t projected = current - static_cast<int64_t>(open_sell) - static_cast<int64_t>(order_qty);
            if (projected < -max_short_) {
                return RiskResult::reject("position limit exceeded");
            }
        }

        return RiskResult::pass();
    }

private:
    PositionTracker& tracker_;
    int64_t max_long_;
    int64_t max_short_;
};

} // namespace oms
