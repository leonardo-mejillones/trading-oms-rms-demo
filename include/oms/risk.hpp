#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

// Per-account token-bucket rate limit. Consumes one token per evaluated order.
// Uses an injectable clock for deterministic testing.
class RateLimitCheck : public RiskCheck {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    RateLimitCheck(uint32_t capacity, double refill_per_second,
                   Clock clock = []() { return std::chrono::steady_clock::now(); })
        : capacity_(static_cast<double>(capacity)),
          refill_per_second_(refill_per_second),
          clock_(std::move(clock)) {}

    [[nodiscard]] RiskResult check(const Order& order) const override {
        const auto now = clock_();
        Bucket& bucket = buckets_[order.account()];
        if (!bucket.initialized) {
            bucket.tokens = capacity_;
            bucket.last = now;
            bucket.initialized = true;
        } else {
            const std::chrono::duration<double> elapsed = now - bucket.last;
            if (elapsed.count() > 0.0) {
                bucket.tokens = std::min(capacity_, bucket.tokens + elapsed.count() * refill_per_second_);
                bucket.last = now;
            }
        }

        if (bucket.tokens < 1.0) {
            return RiskResult::reject("rate limit exceeded");
        }

        bucket.tokens -= 1.0;
        return RiskResult::pass();
    }

private:
    struct Bucket {
        double tokens{0.0};
        std::chrono::steady_clock::time_point last{};
        bool initialized{false};
    };

    double capacity_;
    double refill_per_second_;
    Clock clock_;
    mutable std::unordered_map<AccountId, Bucket> buckets_;
};

} // namespace oms
