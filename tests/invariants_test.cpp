#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "oms/order_book.hpp"
#include "oms/event_sink.hpp"

using namespace oms;

namespace {

// Naive reference implementation: simple and obviously correct, used to cross-check
// the optimized OrderBook behavior.
struct NaiveRestingOrder {
    uint64_t seq{0}; // Monotonic insertion sequence for FIFO priority.
    OrderId id{};
    AccountId account{};
    Side side{};
    Price price{};
    Quantity qty{};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
};

class NaiveOrderBook {
public:
    void submit(const Order& order, EventSink& sink) {
        sink.publish(OrderAccepted{order.id()});

        Quantity remaining = order.quantity();
        if (order.side() == Side::Buy) {
            match_buy(order, remaining, sink);
        } else {
            match_sell(order, remaining, sink);
        }

        if (remaining.value > 0) {
            if (order.type() == OrderType::Market || order.time_in_force() == TimeInForce::IOC) {
                sink.publish(OrderCanceled{order.id()});
            } else {
                resting_.push_back(NaiveRestingOrder{
                    next_seq_++, order.id(), order.account(), order.side(), order.price(), remaining,
                    order.type(), order.time_in_force()});
            }
        }
    }

    void cancel(OrderId order_id, EventSink& sink) {
        auto it = find_by_id(order_id);
        if (it == resting_.end()) {
            sink.publish(CancelRejected{order_id, "unknown or already filled order"});
            return;
        }
        resting_.erase(it);
        sink.publish(OrderCanceled{order_id});
    }

    void replace(OrderId order_id, Price new_price, Quantity new_quantity, EventSink& sink) {
        auto it = find_by_id(order_id);
        if (it == resting_.end()) {
            sink.publish(CancelRejected{order_id, "unknown or already filled order"});
            return;
        }
        if (new_quantity.value == 0) {
            sink.publish(CancelRejected{order_id, "replace quantity must be positive"});
            return;
        }

        const bool same_price = new_price == it->price;
        const bool size_decrease = new_quantity < it->qty;
        const bool no_change = same_price && new_quantity.value == it->qty.value;

        if (no_change) {
            return;
        }

        if (same_price && size_decrease) {
            it->qty = new_quantity; // Keep FIFO priority.
            return;
        }

        // Price change or size increase loses time priority.
        // Re-submit replacement so crossing prices are matched immediately.
        NaiveRestingOrder replacement = *it;
        resting_.erase(it);
        submit(Order(replacement.id, replacement.account, replacement.side, new_price, new_quantity,
                     replacement.type, replacement.tif),
               sink);
    }

    [[nodiscard]] std::optional<Price> best_bid() const {
        std::optional<Price> best;
        for (const auto& o : resting_) {
            if (o.side != Side::Buy) continue;
            if (!best.has_value() || o.price > *best) best = o.price;
        }
        return best;
    }

    [[nodiscard]] std::optional<Price> best_ask() const {
        std::optional<Price> best;
        for (const auto& o : resting_) {
            if (o.side != Side::Sell) continue;
            if (!best.has_value() || o.price < *best) best = o.price;
        }
        return best;
    }

    [[nodiscard]] Quantity depth_at(Side side, Price price) const {
        uint64_t total = 0;
        for (const auto& o : resting_) {
            if (o.side == side && o.price == price) {
                total += o.qty.value;
            }
        }
        return Quantity{total};
    }

    [[nodiscard]] uint64_t total_resting_qty() const {
        uint64_t total = 0;
        for (const auto& o : resting_) total += o.qty.value;
        return total;
    }

private:
    std::vector<NaiveRestingOrder> resting_;
    uint64_t next_seq_{1};

    std::vector<NaiveRestingOrder>::iterator find_by_id(OrderId id) {
        return std::find_if(resting_.begin(), resting_.end(),
                            [&](const NaiveRestingOrder& o) { return o.id == id; });
    }

    void match_buy(const Order& incoming, Quantity& remaining, EventSink& sink) {
        while (remaining.value > 0) {
            auto it = select_best_ask(incoming);
            if (it == resting_.end()) break;

            const Quantity fill_qty = remaining < it->qty ? remaining : it->qty;
            const Price fill_price = it->price;
            sink.publish(Fill{incoming.id(), fill_price, fill_qty});
            sink.publish(Fill{it->id, fill_price, fill_qty});

            remaining = remaining - fill_qty;
            if (it->qty.value == fill_qty.value) {
                resting_.erase(it);
            } else {
                it->qty = it->qty - fill_qty;
            }
        }
    }

    void match_sell(const Order& incoming, Quantity& remaining, EventSink& sink) {
        while (remaining.value > 0) {
            auto it = select_best_bid(incoming);
            if (it == resting_.end()) break;

            const Quantity fill_qty = remaining < it->qty ? remaining : it->qty;
            const Price fill_price = it->price;
            sink.publish(Fill{incoming.id(), fill_price, fill_qty});
            sink.publish(Fill{it->id, fill_price, fill_qty});

            remaining = remaining - fill_qty;
            if (it->qty.value == fill_qty.value) {
                resting_.erase(it);
            } else {
                it->qty = it->qty - fill_qty;
            }
        }
    }

    std::vector<NaiveRestingOrder>::iterator select_best_ask(const Order& incoming) {
        auto best = resting_.end();
        for (auto it = resting_.begin(); it != resting_.end(); ++it) {
            if (it->side != Side::Sell) continue;
            if (incoming.type() == OrderType::Limit && it->price > incoming.price()) continue;
            if (best == resting_.end() || it->price < best->price ||
                (it->price == best->price && it->seq < best->seq)) {
                best = it;
            }
        }
        return best;
    }

    std::vector<NaiveRestingOrder>::iterator select_best_bid(const Order& incoming) {
        auto best = resting_.end();
        for (auto it = resting_.begin(); it != resting_.end(); ++it) {
            if (it->side != Side::Buy) continue;
            if (incoming.type() == OrderType::Limit && it->price < incoming.price()) continue;
            if (best == resting_.end() || it->price > best->price ||
                (it->price == best->price && it->seq < best->seq)) {
                best = it;
            }
        }
        return best;
    }
};

void assert_never_crossed(const OrderBook& book) {
    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    if (bid.has_value() && ask.has_value()) {
        EXPECT_LT(bid->ticks, ask->ticks) << "Book crossed: bid=" << bid->ticks << " ask=" << ask->ticks;
    }
}

void assert_never_crossed(const NaiveOrderBook& book) {
    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    if (bid.has_value() && ask.has_value()) {
        EXPECT_LT(bid->ticks, ask->ticks) << "Naive book crossed: bid=" << bid->ticks << " ask=" << ask->ticks;
    }
}

Order make_random_limit(uint64_t id, std::mt19937_64& rng) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int64_t> price_dist(90, 110);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 20);
    std::uniform_int_distribution<int> tif_dist(0, 1);

    const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
    const TimeInForce tif = tif_dist(rng) == 0 ? TimeInForce::GTC : TimeInForce::IOC;
    return Order(OrderId{id}, AccountId{1}, side, Price{price_dist(rng)}, Quantity{qty_dist(rng)},
                 OrderType::Limit, tif);
}

Order make_random_market(uint64_t id, std::mt19937_64& rng) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 20);
    const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
    return Order(OrderId{id}, AccountId{1}, side, Price{0}, Quantity{qty_dist(rng)}, OrderType::Market,
                 TimeInForce::IOC);
}

} // namespace

TEST(InvariantsTest, RandomizedOrderFlowMatchesNaiveReference) {
    constexpr uint64_t kSeed = 0xC0FFEEULL;
    constexpr int kIterations = 500;

    std::mt19937_64 rng(kSeed);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int64_t> price_dist(90, 110);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 20);

    OrderBook book;
    NaiveOrderBook naive;

    uint64_t next_order_id = 1;
    std::vector<OrderId> known_ids;

    for (int i = 0; i < kIterations; ++i) {
        const int op = op_dist(rng);

        InMemoryEventSink sink_real;
        InMemoryEventSink sink_naive;

        if (op < 55) {
            // Submit limit order.
            Order order = make_random_limit(next_order_id++, rng);
            known_ids.push_back(order.id());
            book.submit(order, sink_real);
            naive.submit(order, sink_naive);
        } else if (op < 70) {
            // Submit market order.
            Order order = make_random_market(next_order_id++, rng);
            known_ids.push_back(order.id());
            book.submit(order, sink_real);
            naive.submit(order, sink_naive);
        } else if (op < 85) {
            // Cancel a random known id (may be unknown/filled, which is valid).
            if (!known_ids.empty()) {
                std::uniform_int_distribution<size_t> pick(0, known_ids.size() - 1);
                const OrderId id = known_ids[pick(rng)];
                book.cancel(id, sink_real);
                naive.cancel(id, sink_naive);
            }
        } else {
            // Replace a random known id.
            if (!known_ids.empty()) {
                std::uniform_int_distribution<size_t> pick(0, known_ids.size() - 1);
                const OrderId id = known_ids[pick(rng)];
                const Price np{price_dist(rng)};
                const Quantity nq{qty_dist(rng)};
                book.replace(id, np, nq, sink_real);
                naive.replace(id, np, nq, sink_naive);
            }
        }

        // FIFO/behavior check: real event sequence must match naive reference exactly.
        ASSERT_EQ(sink_real.events().size(), sink_naive.events().size()) << "iteration=" << i;
        for (size_t e = 0; e < sink_real.events().size(); ++e) {
            ASSERT_EQ(sink_real.events()[e], sink_naive.events()[e])
                << "iteration=" << i << " event=" << e;
        }

        // Invariant: book never crossed after matching.
        assert_never_crossed(book);
        assert_never_crossed(naive);

        // Observable state must match naive reference.
        EXPECT_EQ(book.best_bid(), naive.best_bid()) << "iteration=" << i;
        EXPECT_EQ(book.best_ask(), naive.best_ask()) << "iteration=" << i;
    }

    // Final depth cross-check across a wide price range.
    uint64_t real_resting = 0;
    uint64_t naive_resting = 0;
    for (int64_t p = 80; p <= 120; ++p) {
        const Quantity real_bid = book.depth_at(Side::Buy, Price{p});
        const Quantity naive_bid = naive.depth_at(Side::Buy, Price{p});
        const Quantity real_ask = book.depth_at(Side::Sell, Price{p});
        const Quantity naive_ask = naive.depth_at(Side::Sell, Price{p});
        EXPECT_EQ(real_bid, naive_bid) << "price=" << p << " side=Buy";
        EXPECT_EQ(real_ask, naive_ask) << "price=" << p << " side=Sell";
        real_resting += real_bid.value + real_ask.value;
        naive_resting += naive_bid.value + naive_ask.value;
    }

    // Quantity conservation/invariant: total resting quantity matches naive reference.
    EXPECT_EQ(real_resting, naive_resting);
    EXPECT_EQ(real_resting, naive.total_resting_qty());
}
