#include <gtest/gtest.h>

#include <vector>

#include "oms/order_state.hpp"

using namespace oms;

TEST(OrderStateTest, AllowedTransitions) {
    struct Case {
        OrderState from;
        OrderTrigger trigger;
        OrderState expected;
    };

    const std::vector<Case> cases = {
        {OrderState::New,             OrderTrigger::Accept,      OrderState::Accepted},
        {OrderState::New,             OrderTrigger::Reject,      OrderState::Rejected},

        {OrderState::Accepted,        OrderTrigger::PartialFill, OrderState::PartiallyFilled},
        {OrderState::Accepted,        OrderTrigger::Fill,        OrderState::Filled},
        {OrderState::Accepted,        OrderTrigger::Cancel,      OrderState::Canceled},

        {OrderState::PartiallyFilled, OrderTrigger::PartialFill, OrderState::PartiallyFilled},
        {OrderState::PartiallyFilled, OrderTrigger::Fill,        OrderState::Filled},
        {OrderState::PartiallyFilled, OrderTrigger::Cancel,      OrderState::Canceled},
    };

    for (const auto& c : cases) {
        EXPECT_TRUE(can_transition(c.from, c.trigger))
            << to_string(c.from) << " + " << to_string(c.trigger);
        const auto result = apply_transition(c.from, c.trigger);
        EXPECT_TRUE(result.ok()) << result.error;
        EXPECT_EQ(result.state, c.expected)
            << to_string(c.from) << " + " << to_string(c.trigger);
        EXPECT_TRUE(result.error.empty());
    }
}

TEST(OrderStateTest, IllegalTransitionsReturnError) {
    const std::vector<OrderState> all_states = {
        OrderState::New, OrderState::Accepted, OrderState::PartiallyFilled,
        OrderState::Filled, OrderState::Canceled, OrderState::Rejected,
    };
    const std::vector<OrderTrigger> all_triggers = {
        OrderTrigger::Accept, OrderTrigger::Reject, OrderTrigger::PartialFill,
        OrderTrigger::Fill, OrderTrigger::Cancel,
    };

    // Allowed pairs from the transition table.
    const auto is_allowed = [](OrderState s, OrderTrigger t) {
        return (s == OrderState::New && (t == OrderTrigger::Accept || t == OrderTrigger::Reject)) ||
               (s == OrderState::Accepted && (t == OrderTrigger::PartialFill || t == OrderTrigger::Fill || t == OrderTrigger::Cancel)) ||
               (s == OrderState::PartiallyFilled && (t == OrderTrigger::PartialFill || t == OrderTrigger::Fill || t == OrderTrigger::Cancel));
    };

    for (const auto state : all_states) {
        for (const auto trigger : all_triggers) {
            if (is_allowed(state, trigger)) {
                continue;
            }
            EXPECT_FALSE(can_transition(state, trigger))
                << to_string(state) << " + " << to_string(trigger);
            const auto result = apply_transition(state, trigger);
            EXPECT_FALSE(result.ok())
                << to_string(state) << " + " << to_string(trigger);
            EXPECT_EQ(result.state, state) << "illegal transition should keep current state";
            EXPECT_FALSE(result.error.empty());
        }
    }
}

TEST(OrderStateTest, TerminalStatesHaveNoOutgoingTransitions) {
    const std::vector<OrderState> terminal = {
        OrderState::Filled, OrderState::Canceled, OrderState::Rejected,
    };
    const std::vector<OrderTrigger> all_triggers = {
        OrderTrigger::Accept, OrderTrigger::Reject, OrderTrigger::PartialFill,
        OrderTrigger::Fill, OrderTrigger::Cancel,
    };

    for (const auto state : terminal) {
        for (const auto trigger : all_triggers) {
            EXPECT_FALSE(can_transition(state, trigger));
            EXPECT_FALSE(apply_transition(state, trigger).ok());
        }
    }
}

TEST(OrderStateTest, ToString) {
    EXPECT_EQ(to_string(OrderState::New), "New");
    EXPECT_EQ(to_string(OrderState::Accepted), "Accepted");
    EXPECT_EQ(to_string(OrderState::PartiallyFilled), "PartiallyFilled");
    EXPECT_EQ(to_string(OrderState::Filled), "Filled");
    EXPECT_EQ(to_string(OrderState::Canceled), "Canceled");
    EXPECT_EQ(to_string(OrderState::Rejected), "Rejected");
    EXPECT_EQ(to_string(OrderTrigger::Accept), "Accept");
    EXPECT_EQ(to_string(OrderTrigger::Reject), "Reject");
    EXPECT_EQ(to_string(OrderTrigger::PartialFill), "PartialFill");
    EXPECT_EQ(to_string(OrderTrigger::Fill), "Fill");
    EXPECT_EQ(to_string(OrderTrigger::Cancel), "Cancel");
}
