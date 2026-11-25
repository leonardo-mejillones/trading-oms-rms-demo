#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "oms/matching_engine.hpp"

namespace oms {

struct OrderFlowConfig {
    uint64_t seed{1};
    std::size_t count{1000};
    std::string symbol{"AAPL"};
    AccountId account{1};
    int64_t min_price{90};
    int64_t max_price{110};
    uint64_t min_qty{1};
    uint64_t max_qty{20};
    int new_weight{60};        // passive new limit orders
    int aggressive_weight{25}; // aggressive/crossing limit orders
    int cancel_weight{15};     // cancel existing orders
};

// Generates a reproducible synthetic order flow for benchmarks/tests.
// The mix contains new passive limits, aggressive crossing limits, and cancels.
class OrderFlowGenerator {
public:
    [[nodiscard]] std::vector<EngineCommand> generate(const OrderFlowConfig& config) const;
};

} // namespace oms