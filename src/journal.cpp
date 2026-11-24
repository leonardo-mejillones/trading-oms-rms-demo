#include "oms/journal.hpp"

#include <type_traits>

namespace oms {

void EventJournal::append_submit(const Order& order) {
    entries_.push_back(SubmitEntry{order});
}

void EventJournal::append_cancel(OrderId order_id) {
    entries_.push_back(CancelEntry{order_id});
}

void EventJournal::append_replace(OrderId order_id, Price new_price, Quantity new_quantity) {
    entries_.push_back(ReplaceEntry{order_id, new_price, new_quantity});
}

void EventJournal::replay(OrderBook& book, EventSink& sink) const {
    for (const auto& entry : entries_) {
        std::visit([&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, SubmitEntry>) {
                book.submit(e.order, sink);
            } else if constexpr (std::is_same_v<T, CancelEntry>) {
                book.cancel(e.order_id, sink);
            } else if constexpr (std::is_same_v<T, ReplaceEntry>) {
                book.replace(e.order_id, e.new_price, e.new_quantity, sink);
            }
        }, entry);
    }
}

} // namespace oms
