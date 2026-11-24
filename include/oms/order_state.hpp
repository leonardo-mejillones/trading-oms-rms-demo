#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace oms {

enum class OrderState : uint8_t {
    New,
    Accepted,
    PartiallyFilled,
    Filled,
    Canceled,
    Rejected
};

enum class OrderTrigger : uint8_t {
    Accept,
    Reject,
    PartialFill,
    Fill,
    Cancel
};

struct TransitionResult {
    OrderState state;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

constexpr std::string_view to_string(OrderState state) noexcept {
    switch (state) {
        case OrderState::New:             return "New";
        case OrderState::Accepted:        return "Accepted";
        case OrderState::PartiallyFilled: return "PartiallyFilled";
        case OrderState::Filled:          return "Filled";
        case OrderState::Canceled:        return "Canceled";
        case OrderState::Rejected:        return "Rejected";
    }
    return "Unknown";
}

constexpr std::string_view to_string(OrderTrigger trigger) noexcept {
    switch (trigger) {
        case OrderTrigger::Accept:      return "Accept";
        case OrderTrigger::Reject:      return "Reject";
        case OrderTrigger::PartialFill: return "PartialFill";
        case OrderTrigger::Fill:        return "Fill";
        case OrderTrigger::Cancel:      return "Cancel";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, OrderState state) {
    return os << to_string(state);
}

inline std::ostream& operator<<(std::ostream& os, OrderTrigger trigger) {
    return os << to_string(trigger);
}

/// Applies a lifecycle trigger to the current order state.
/// Returns the next state on success, or an error describing the illegal transition.
[[nodiscard]] TransitionResult apply_transition(OrderState current, OrderTrigger trigger);

/// Returns true when the transition is allowed by the lifecycle table.
[[nodiscard]] bool can_transition(OrderState current, OrderTrigger trigger);

} // namespace oms
