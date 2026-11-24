#include <gtest/gtest.h>

#include "oms/journal.hpp"
#include "oms/event_sink.hpp"

using namespace oms;

namespace {

Order make_limit(uint64_t id, Side side, int64_t price, uint64_t qty,
                 TimeInForce tif = TimeInForce::GTC, uint64_t account = 1) {
    return Order(OrderId{id}, AccountId{account}, side, Price{price}, Quantity{qty},
                 OrderType::Limit, tif);
}

void assert_books_equal(const OrderBook& a, const OrderBook& b) {
    EXPECT_EQ(a.best_bid(), b.best_bid());
    EXPECT_EQ(a.best_ask(), b.best_ask());
    for (int64_t p = 1; p <= 200; ++p) {
        EXPECT_EQ(a.depth_at(Side::Buy, Price{p}), b.depth_at(Side::Buy, Price{p})) << "price=" << p;
        EXPECT_EQ(a.depth_at(Side::Sell, Price{p}), b.depth_at(Side::Sell, Price{p})) << "price=" << p;
    }
}

} // namespace

TEST(JournalTest, AppendOnlyRecordsEntriesInOrder) {
    EventJournal journal;
    EXPECT_TRUE(journal.empty());
    EXPECT_EQ(journal.size(), 0u);

    journal.append_submit(make_limit(1, Side::Buy, 100, 10));
    journal.append_cancel(OrderId{1});
    journal.append_replace(OrderId{2}, Price{101}, Quantity{5});

    EXPECT_FALSE(journal.empty());
    EXPECT_EQ(journal.size(), 3u);

    ASSERT_TRUE(std::holds_alternative<SubmitEntry>(journal.entries()[0]));
    ASSERT_TRUE(std::holds_alternative<CancelEntry>(journal.entries()[1]));
    ASSERT_TRUE(std::holds_alternative<ReplaceEntry>(journal.entries()[2]));

    EXPECT_EQ(std::get<CancelEntry>(journal.entries()[1]).order_id, OrderId{1});
    const auto& repl = std::get<ReplaceEntry>(journal.entries()[2]);
    EXPECT_EQ(repl.order_id, OrderId{2});
    EXPECT_EQ(repl.new_price, Price{101});
    EXPECT_EQ(repl.new_quantity, Quantity{5});
}

TEST(JournalTest, ReplayReproducesIdenticalBook) {
    EventJournal journal;
    OrderBook original;
    InMemoryEventSink original_sink;

    // Build a sequence of operations and record them in the journal.
    auto submit = [&](const Order& o) {
        journal.append_submit(o);
        original.submit(o, original_sink);
    };
    auto cancel = [&](OrderId id) {
        journal.append_cancel(id);
        original.cancel(id, original_sink);
    };
    auto replace = [&](OrderId id, Price p, Quantity q) {
        journal.append_replace(id, p, q);
        original.replace(id, p, q, original_sink);
    };

    submit(make_limit(1, Side::Buy, 100, 10));
    submit(make_limit(2, Side::Buy, 99, 5));
    submit(make_limit(3, Side::Sell, 105, 7));
    submit(make_limit(4, Side::Sell, 106, 3));
    submit(make_limit(5, Side::Buy, 105, 4)); // crosses partially
    cancel(OrderId{2});
    replace(OrderId{1}, Price{100}, Quantity{8}); // size decrease keeps priority? same price, decrease
    submit(make_limit(6, Side::Sell, 100, 2)); // crosses with resting buy
    replace(OrderId{3}, Price{104}, Quantity{7}); // price change loses priority, may cross? ask 104 vs bid 100 no

    OrderBook replayed;
    InMemoryEventSink replay_sink;
    journal.replay(replayed, replay_sink);

    assert_books_equal(original, replayed);
    EXPECT_EQ(original_sink.events().size(), replay_sink.events().size());
    EXPECT_EQ(original_sink.events(), replay_sink.events());
}
