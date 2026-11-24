#include "oms/oms.hpp"

namespace oms {

namespace {

class CaptureSink : public EventSink {
public:
    explicit CaptureSink(EventSink& downstream) : downstream_(downstream) {}

    void publish(const Event& event) override {
        downstream_.publish(event);
        events_.push_back(event);
    }

    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }

private:
    EventSink& downstream_;
    std::vector<Event> events_;
};

} // namespace

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

    // Pre-trade risk checks run before the order reaches the book.
    if (risk_pipeline_ != nullptr) {
        const RiskResult risk = risk_pipeline_->evaluate(order);
        if (!risk.ok) {
            const Event rejection = OrderRejected{order.id(), risk.reason};
            sink.publish(rejection);
            if (has_client_id) {
                idempotency_store_.emplace(key, std::vector<Event>{rejection});
            }
            return;
        }
    }

    if (has_client_id) {
        CaptureSink capture(sink);
        book_.submit(order, capture);
        idempotency_store_.emplace(key, capture.events());
        return;
    }

    book_.submit(order, sink);
}

void Oms::cancel(OrderId order_id, EventSink& sink) {
    book_.cancel(order_id, sink);
}

void Oms::replace(OrderId order_id, Price new_price, Quantity new_quantity, EventSink& sink) {
    book_.replace(order_id, new_price, new_quantity, sink);
}

} // namespace oms
