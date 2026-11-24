#include "oms/validation.hpp"

namespace oms {

std::optional<RejectReason> validate_order(
    const OrderRequest& request,
    const std::unordered_map<std::string, InstrumentSpec>& instruments) {
    if (request.quantity.value == 0) {
        return RejectReason::InvalidQuantity;
    }

    if (request.type == OrderType::Limit && request.price.ticks <= 0) {
        return RejectReason::InvalidPrice;
    }

    const auto it = instruments.find(request.symbol);
    if (it == instruments.end()) {
        return RejectReason::UnknownSymbol;
    }

    const auto& spec = it->second;

    if (spec.tick_size <= 0 || (request.type == OrderType::Limit && (request.price.ticks % spec.tick_size) != 0)) {
        return RejectReason::InvalidTickSize;
    }

    if (spec.lot_size == 0 || (request.quantity.value % spec.lot_size) != 0) {
        return RejectReason::InvalidLotSize;
    }

    return std::nullopt;
}

std::optional<RejectReason> validate_order(
    const Order& order,
    const std::unordered_map<std::string, InstrumentSpec>& instruments) {
    return validate_order(
        OrderRequest{order.id(), order.account(), order.client_order_id(), order.symbol(), order.side(),
                     order.price(), order.quantity(), order.type(), order.time_in_force()},
        instruments);
}

} // namespace oms
