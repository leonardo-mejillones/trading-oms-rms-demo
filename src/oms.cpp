#include "oms/oms.hpp"

namespace oms {

namespace {

class CaptureSink : public EventSink {
public:
    explicit CaptureSink(EventSink& downstream) : downstream_(downstream) {}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    void publish(const Event& event) override {
        downstream_.publish(event);
        events_.push_back(event);
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }

private:
    EventSink& downstream_;
    std::vector<Event> events_;
};

} // namespace

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void Oms::submit(const Order& order, EventSink& sink) {
    const bool has_client_id = order.client_order_id().value != 0;
    ClientOrderKey key{};
    if (has_client_id) {
        key = ClientOrderKey{order.account(), order.client_order_id()};
        if (const auto it = idempotency_store_.find(key); it != idempotency_store_.end()) {
            // Duplicate ClientOrderId for same account: replay original result.
            for (const auto& event : it->second) {
                sink.publish(event);
            }
            return;
        }
    }

    if (halted_accounts_.contains(order.account())) {
        const Event rejection = OrderRejected{order.id(), "account halted"};
        sink.publish(rejection);
        if (has_client_id) {
            idempotency_store_.emplace(key, std::vector<Event>{rejection});
        }
        return;
    }

    if (position_tracker_ != nullptr) {
        position_tracker_->register_order(order);
    }

    // Pre-trade risk checks run before the order reaches the book.
    if (risk_pipeline_ != nullptr) {
        const RiskResult risk = risk_pipeline_->evaluate(order);
        if (!risk.ok) {
            const Event rejection = OrderRejected{order.id(), risk.reason};
            sink.publish(rejection);
            if (position_tracker_ != nullptr) {
                position_tracker_->apply(rejection);
            }
            if (has_client_id) {
                idempotency_store_.emplace(key, std::vector<Event>{rejection});
            }
            return;
        }
    }

    if (has_client_id || position_tracker_ != nullptr) {
        CaptureSink capture(sink);
        book_.submit(order, capture);
        if (position_tracker_ != nullptr) {
            for (const auto& event : capture.events()) {
                position_tracker_->apply(event);
            }
        }
        if (has_client_id) {
            idempotency_store_.emplace(key, capture.events());
        }
        return;
    }

    book_.submit(order, sink);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void Oms::cancel(OrderId order_id, EventSink& sink) {
    if (position_tracker_ != nullptr) {
        CaptureSink capture(sink);
        book_.cancel(order_id, capture);
        for (const auto& event : capture.events()) {
            position_tracker_->apply(event);
        }
        return;
    }
    book_.cancel(order_id, sink);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void Oms::replace(OrderId order_id, Price new_price, Quantity new_quantity, EventSink& sink) {
    if (const auto account = book_.account_of(order_id);
        account.has_value() && halted_accounts_.contains(*account)) {
        sink.publish(OrderRejected{order_id, "account halted"});
        return;
    }

    if (position_tracker_ == nullptr) {
        book_.replace(order_id, new_price, new_quantity, sink);
        return;
    }

    const auto old_info = position_tracker_->open_order(order_id);
    CaptureSink capture(sink);
    book_.replace(order_id, new_price, new_quantity, capture);

    bool saw_accepted = false;
    for (const auto& event : capture.events()) {
        if (const auto* accepted = std::get_if<OrderAccepted>(&event)) {
            if (accepted->order_id == order_id && old_info.has_value()) {
                // Losing-priority replace re-submitted the order: re-register with updated qty/price.
                position_tracker_->close_open_order(order_id);
                position_tracker_->register_order(Order(order_id, old_info->account, old_info->side, new_price,
                                                       new_quantity, OrderType::Limit, TimeInForce::GTC,
                                                       ClientOrderId{0}, old_info->symbol));
                saw_accepted = true;
            }
        }
        position_tracker_->apply(event);
    }

    // In-place decrease/no-op emits no events; sync tracker quantity if still open.
    if (capture.events().empty() && old_info.has_value()) {
        position_tracker_->update_open_quantity(order_id, new_quantity);
    }

    (void)saw_accepted;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace oms
