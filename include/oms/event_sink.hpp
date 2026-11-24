#pragma once

#include <cstddef>
#include <vector>

#include "oms/events.hpp"

namespace oms {

// Interface for consuming domain events emitted by OMS components.
class EventSink {
public:
    virtual ~EventSink() = default;

    virtual void publish(const Event& event) = 0;
};

// Simple in-memory sink used by tests to inspect emitted events.
class InMemoryEventSink : public EventSink {
public:
    void publish(const Event& event) override {
        events_.push_back(event);
    }

    [[nodiscard]] const std::vector<Event>& events() const noexcept {
        return events_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return events_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return events_.empty();
    }

    void clear() noexcept {
        events_.clear();
    }

    // Returns all events of a specific type in publication order.
    template <typename T>
    [[nodiscard]] std::vector<T> events_of_type() const {
        std::vector<T> result;
        for (const auto& event : events_) {
            if (const auto* value = std::get_if<T>(&event)) {
                result.push_back(*value);
            }
        }
        return result;
    }

private:
    std::vector<Event> events_;
};

} // namespace oms
