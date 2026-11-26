#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "oms/feed_handler.hpp"

using namespace oms;

namespace {

Quote make_quote(uint64_t seq, const std::string& symbol = "AAPL") {
    Quote q;
    q.sequence = seq;
    q.symbol = symbol;
    q.bid = Price{100};
    q.ask = Price{101};
    q.bid_qty = Quantity{10};
    q.ask_qty = Quantity{10};
    return q;
}

std::vector<std::uint8_t> datagram(uint64_t seq) {
    return encode_quote(make_quote(seq));
}

} // namespace

TEST(FeedHandlerTest, ParsesAndPushesInOrderQuotes) {
    SpscRingBuffer<Quote> ring(8);
    FeedHandler handler(ring, 1);

    const auto d1 = datagram(1);
    const auto d2 = datagram(2);
    EXPECT_TRUE(handler.handle_datagram(d1.data(), d1.size()));
    EXPECT_TRUE(handler.handle_datagram(d2.data(), d2.size()));

    EXPECT_EQ(handler.stats().received, 2u);
    EXPECT_EQ(handler.stats().pushed, 2u);
    EXPECT_EQ(handler.stats().gaps, 0u);
    EXPECT_EQ(handler.stats().duplicates, 0u);
    EXPECT_EQ(handler.stats().invalid, 0u);
    EXPECT_EQ(handler.expected_sequence(), 3u);

    auto q1 = ring.try_pop();
    auto q2 = ring.try_pop();
    ASSERT_TRUE(q1.has_value());
    ASSERT_TRUE(q2.has_value());
    EXPECT_EQ(q1->sequence, 1u);
    EXPECT_EQ(q2->sequence, 2u);
}

TEST(FeedHandlerTest, InvalidDatagramIsCountedAndNotPushed) {
    SpscRingBuffer<Quote> ring(4);
    FeedHandler handler(ring, 1);

    const std::uint8_t bad[] = {1, 2, 3};
    EXPECT_FALSE(handler.handle_datagram(bad, sizeof(bad)));

    EXPECT_EQ(handler.stats().invalid, 1u);
    EXPECT_EQ(handler.stats().received, 0u);
    EXPECT_EQ(handler.stats().pushed, 0u);
    EXPECT_FALSE(ring.try_pop().has_value());
}

TEST(FeedHandlerTest, DetectsGapAndPushesReceivedQuote) {
    SpscRingBuffer<Quote> ring(8);
    FeedHandler handler(ring, 1);

    const auto d1 = datagram(1);
    const auto d4 = datagram(4); // missing 2,3
    EXPECT_TRUE(handler.handle_datagram(d1.data(), d1.size()));
    EXPECT_TRUE(handler.handle_datagram(d4.data(), d4.size()));

    EXPECT_EQ(handler.stats().gaps, 1u);
    EXPECT_EQ(handler.stats().missing, 2u);
    EXPECT_EQ(handler.stats().pushed, 2u);
    EXPECT_EQ(handler.expected_sequence(), 5u);

    auto q1 = ring.try_pop();
    auto q4 = ring.try_pop();
    ASSERT_TRUE(q1.has_value());
    ASSERT_TRUE(q4.has_value());
    EXPECT_EQ(q1->sequence, 1u);
    EXPECT_EQ(q4->sequence, 4u);
}

TEST(FeedHandlerTest, DuplicateOrLateSequenceIsNotPushed) {
    SpscRingBuffer<Quote> ring(8);
    FeedHandler handler(ring, 1);

    const auto d1 = datagram(1);
    const auto d2 = datagram(2);
    const auto dup = datagram(1);
    EXPECT_TRUE(handler.handle_datagram(d1.data(), d1.size()));
    EXPECT_TRUE(handler.handle_datagram(d2.data(), d2.size()));
    EXPECT_FALSE(handler.handle_datagram(dup.data(), dup.size()));

    EXPECT_EQ(handler.stats().duplicates, 1u);
    EXPECT_EQ(handler.stats().pushed, 2u);
    EXPECT_EQ(handler.expected_sequence(), 3u);
}

TEST(FeedHandlerTest, RingBufferFullDropsQuote) {
    SpscRingBuffer<Quote> ring(1); // capacity 1
    FeedHandler handler(ring, 1);

    const auto d1 = datagram(1);
    const auto d2 = datagram(2);
    EXPECT_TRUE(handler.handle_datagram(d1.data(), d1.size()));
    EXPECT_FALSE(handler.handle_datagram(d2.data(), d2.size())); // ring full

    EXPECT_EQ(handler.stats().pushed, 1u);
    EXPECT_EQ(handler.stats().dropped, 1u);
    EXPECT_EQ(handler.expected_sequence(), 3u); // sequence still advanced
}