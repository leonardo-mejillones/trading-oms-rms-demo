#include "oms/matching_engine.hpp"

#include <stdexcept>
#include <type_traits>

namespace oms {

void MatchingEngine::process(const EngineCommand& command, EventSink& sink) {
    std::visit(
        [this, &sink](const auto& cmd) {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, SubmitCommand>) {
                process_submit(cmd, sink);
            } else if constexpr (std::is_same_v<T, CancelCommand>) {
                process_cancel(cmd, sink);
            } else if constexpr (std::is_same_v<T, ReplaceCommand>) {
                process_replace(cmd, sink);
            }
        },
        command);
}

void MatchingEngine::process_submit(const SubmitCommand& cmd, EventSink& sink) {
    const auto& request = cmd.request;

    if (const auto reason = validate_order(request, instruments_); reason.has_value()) {
        sink.publish(OrderRejected{request.id, std::string(to_string(*reason))});
        return;
    }

    try {
        const Order order(request.id, request.account, request.side, request.price, request.quantity,
                          request.type, request.tif, request.client_order_id, request.symbol);
        oms_.submit(order, sink);
    } catch (const std::exception&) {
        sink.publish(OrderRejected{request.id, "InvalidOrder"});
    }
}

void MatchingEngine::process_cancel(const CancelCommand& cmd, EventSink& sink) {
    oms_.cancel(cmd.order_id, sink);
}

void MatchingEngine::process_replace(const ReplaceCommand& cmd, EventSink& sink) {
    if (const Order* resting = book().find(cmd.order_id); resting != nullptr) {
        const OrderRequest request{resting->id(), resting->account(), resting->client_order_id(),
                                   resting->symbol(), resting->side(), cmd.new_price, cmd.new_quantity,
                                   resting->type(), resting->time_in_force()};
        if (const auto reason = validate_order(request, instruments_); reason.has_value()) {
            sink.publish(OrderRejected{cmd.order_id, std::string(to_string(*reason))});
            return;
        }
    }

    oms_.replace(cmd.order_id, cmd.new_price, cmd.new_quantity, sink);
}

} // namespace oms