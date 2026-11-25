#include "oms/order_flow_generator.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace oms {

std::vector<EngineCommand> OrderFlowGenerator::generate(const OrderFlowConfig& config) const {
    if (config.max_price < config.min_price) {
        throw std::invalid_argument("max_price must be >= min_price");
    }
    if (config.max_qty < config.min_qty || config.min_qty == 0) {
        throw std::invalid_argument("invalid quantity range");
    }

    const int total_weight = config.new_weight + config.aggressive_weight + config.cancel_weight;
    if (total_weight <= 0) {
        throw std::invalid_argument("at least one weight must be positive");
    }

    std::mt19937_64 rng(config.seed);
    std::uniform_int_distribution<int> op_dist(1, total_weight);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int64_t> passive_price_dist(config.min_price, config.max_price);
    std::uniform_int_distribution<uint64_t> qty_dist(config.min_qty, config.max_qty);

    std::vector<EngineCommand> commands;
    commands.reserve(config.count);

    std::vector<uint64_t> known_ids;
    known_ids.reserve(config.count);

    uint64_t next_id = 1;
    for (std::size_t i = 0; i < config.count; ++i) {
        const int roll = op_dist(rng);
        const bool do_cancel = roll > (config.new_weight + config.aggressive_weight) && !known_ids.empty();
        const bool do_aggressive = !do_cancel && roll > config.new_weight;

        if (do_cancel) {
            std::uniform_int_distribution<std::size_t> pick(0, known_ids.size() - 1);
            const OrderId id{known_ids[pick(rng)]};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
            commands.push_back(CancelCommand{id});
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
            continue;
        }

        const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        const Quantity qty{qty_dist(rng)};
        Price price{passive_price_dist(rng)};
        if (do_aggressive) {
            // Aggressive limit: priced to cross the opposite side.
            price = (side == Side::Buy) ? Price{config.max_price} : Price{config.min_price};
        }

        OrderRequest req;
        req.id = OrderId{next_id};
        req.account = config.account;
        req.client_order_id = ClientOrderId{next_id};
        req.symbol = config.symbol;
        req.side = side;
        req.price = price;
        req.quantity = qty;
        req.type = OrderType::Limit;
        req.tif = TimeInForce::GTC;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        commands.push_back(SubmitCommand{req});
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        known_ids.push_back(next_id);
        ++next_id;
    }

    return commands;
}

} // namespace oms