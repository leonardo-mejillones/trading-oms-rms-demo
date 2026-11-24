#pragma once

#include <cstddef>
#include <variant>
#include <vector>

#include "oms/order_book.hpp"

namespace oms {

struct SubmitEntry {
    Order order;
};

struct CancelEntry {
    OrderId order_id;
};

struct ReplaceEntry {
    OrderId order_id;
    Price new_price;
    Quantity new_quantity;
};

using JournalEntry = std::variant<SubmitEntry, CancelEntry, ReplaceEntry>;

// Append-only journal of order-processing inputs.
// Replaying entries in order on a fresh OrderBook reproduces deterministic state.
class EventJournal {
public:
    void append_submit(const Order& order);
    void append_cancel(OrderId order_id);
    void append_replace(OrderId order_id, Price new_price, Quantity new_quantity);

    [[nodiscard]] const std::vector<JournalEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    // Replays all journal entries in append order into the provided book.
    // For deterministic rebuild, replay into a fresh/empty OrderBook.
    void replay(OrderBook& book, EventSink& sink) const;

private:
    std::vector<JournalEntry> entries_;
};

} // namespace oms
