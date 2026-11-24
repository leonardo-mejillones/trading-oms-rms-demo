#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "oms/order.hpp"

namespace oms {

// Result of a single risk check or the whole pipeline.
struct RiskResult {
    bool ok{true};
    std::string reason;

    [[nodiscard]] static RiskResult pass() { return {true, {}}; }
    [[nodiscard]] static RiskResult reject(std::string r) { return {false, std::move(r)}; }
};

// Composable pre-trade risk check interface.
// Implementations should be stateless or thread-safe as needed by the caller.
class RiskCheck {
public:
    virtual ~RiskCheck() = default;
    virtual RiskResult check(const Order& order) const = 0;
};

// Evaluates risk checks in order; short-circuits on first rejection.
class RiskCheckPipeline {
public:
    void add(std::shared_ptr<RiskCheck> check) {
        checks_.push_back(std::move(check));
    }

    [[nodiscard]] RiskResult evaluate(const Order& order) const {
        for (const auto& check : checks_) {
            if (!check) {
                continue;
            }
            const RiskResult result = check->check(order);
            if (!result.ok) {
                return result; // Short-circuit: do not run remaining checks.
            }
        }
        return RiskResult::pass();
    }

    [[nodiscard]] std::size_t size() const noexcept { return checks_.size(); }
    [[nodiscard]] bool empty() const noexcept { return checks_.empty(); }

private:
    std::vector<std::shared_ptr<RiskCheck>> checks_;
};

// Rejects orders whose quantity exceeds the configured maximum.
class MaxOrderSizeCheck : public RiskCheck {
public:
    explicit MaxOrderSizeCheck(Quantity max_size) : max_size_(max_size) {}

    [[nodiscard]] RiskResult check(const Order& order) const override {
        if (order.quantity() > max_size_) {
            return RiskResult::reject("max order size exceeded");
        }
        return RiskResult::pass();
    }

private:
    Quantity max_size_;
};

// Rejects orders whose notional (price ticks * quantity) exceeds the configured maximum.
// Notional is evaluated for limit orders; market orders have no limit price reference.
class MaxNotionalCheck : public RiskCheck {
public:
    explicit MaxNotionalCheck(int64_t max_notional) : max_notional_(max_notional) {}

    [[nodiscard]] RiskResult check(const Order& order) const override {
        if (order.type() == OrderType::Market) {
            return RiskResult::pass();
        }
        const int64_t notional = order.price().ticks * static_cast<int64_t>(order.quantity().value);
        if (notional > max_notional_) {
            return RiskResult::reject("max order notional exceeded");
        }
        return RiskResult::pass();
    }

private:
    int64_t max_notional_;
};

} // namespace oms
