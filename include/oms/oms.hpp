#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "oms/event_sink.hpp"
#include "oms/order_book.hpp"

namespace oms {

// OMS facade that adds client idempotency on top of the matching engine.
// Duplicate submissions with the same (AccountId, ClientOrderId) return the
// original result without reprocessing.
class Oms {
public:
    void submit(const Order& order, EventSink& sink);
    void cancel(OrderId order_id, EventSink& sink);
    void replace(OrderId order_id, Price new_price, Quantity new_quantity, EventSink& sink);

    [[nodiscard]] OrderBook& book() noexcept { return book_; }
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }

private:
    struct ClientOrderKey {
        AccountId account{};
        ClientOrderId client_order_id{};

        bool operator==(const ClientOrderKey&) const noexcept = default;
    };

    struct ClientOrderKeyHash {
        std::size_t operator()(const ClientOrderKey& key) const noexcept {
            const std::size_t h1 = std::hash<uint64_t>{}(key.account.value);
            const std::size_t h2 = std::hash<uint64_t>{}(key.client_order_id.value);
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
        }
    };

    OrderBook book_;
    std::unordered_map<ClientOrderKey, std::vector<Event>, ClientOrderKeyHash> idempotency_store_;
};

} // namespace oms
