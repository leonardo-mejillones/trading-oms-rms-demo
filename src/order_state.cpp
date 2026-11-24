#include "oms/order_state.hpp"

#include <array>
#include <optional>

namespace oms {

namespace {

struct TransitionEntry {
    OrderState from;
    OrderTrigger trigger;
    OrderState to;
};

// Explicit lifecycle transition table. Any (state, trigger) pair not present here is illegal.
constexpr std::array<TransitionEntry, 9> kTransitionTable{{
    {OrderState::New,             OrderTrigger::Accept,      OrderState::Accepted},
    {OrderState::New,             OrderTrigger::Reject,      OrderState::Rejected},

    {OrderState::Accepted,        OrderTrigger::PartialFill, OrderState::PartiallyFilled},
    {OrderState::Accepted,        OrderTrigger::Fill,        OrderState::Filled},
    {OrderState::Accepted,        OrderTrigger::Cancel,      OrderState::Canceled},

    {OrderState::PartiallyFilled, OrderTrigger::PartialFill, OrderState::PartiallyFilled},
    {OrderState::PartiallyFilled, OrderTrigger::Fill,        OrderState::Filled},
    {OrderState::PartiallyFilled, OrderTrigger::Cancel,      OrderState::Canceled},
}};

std::optional<OrderState> find_transition(OrderState current, OrderTrigger trigger) {
    for (const auto& entry : kTransitionTable) {
        if (entry.from == current && entry.trigger == trigger) {
            return entry.to;
        }
    }
    return std::nullopt;
}

} // namespace

TransitionResult apply_transition(OrderState current, OrderTrigger trigger) {
    const auto next = find_transition(current, trigger);
    if (next.has_value()) {
        return TransitionResult{*next, {}};
    }

    return TransitionResult{
        current,
        "illegal transition: " + std::string(to_string(current)) + " + " +
            std::string(to_string(trigger))};
}

bool can_transition(OrderState current, OrderTrigger trigger) {
    return find_transition(current, trigger).has_value();
}

} // namespace oms
