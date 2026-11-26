#pragma once

#include <cstddef>
#include <cstdint>

#include "oms/market_data.hpp"
#include "oms/spsc_ring_buffer.hpp"

namespace oms {

/// Counters for feed handler processing and sequence validation.
struct FeedHandlerStats {
    uint64_t received{0};    // Datagrams successfully decoded as quotes.
    uint64_t invalid{0};     // Datagrams that could not be decoded.
    uint64_t gaps{0};        // Gap events detected (received sequence > expected).
    uint64_t missing{0};     // Total missing sequence numbers across all gaps.
    uint64_t duplicates{0};  // Datagrams with sequence < expected (late/duplicate).
    uint64_t pushed{0};      // Quotes successfully pushed into the ring buffer.
    uint64_t dropped{0};     // Quotes dropped because the ring buffer was full.
};

/// Receives market data datagrams, validates sequence numbers, detects gaps,
/// and pushes parsed quotes into an SPSC ring buffer.
///
/// Expected behavior:
/// - Decode failure: counted as invalid, no push.
/// - sequence == expected: push quote, expected advances by 1.
/// - sequence > expected: gap detected, missing += sequence-expected,
///   expected becomes sequence+1, quote is pushed.
/// - sequence < expected: counted as duplicate/late, quote is not pushed.
class FeedHandler {
public:
    /// @param out Ring buffer where parsed quotes are pushed (producer side).
    /// @param expected_sequence First sequence number expected.
    FeedHandler(SpscRingBuffer<Quote>& out, uint64_t expected_sequence = 1);

    /// Parses and processes one UDP/datagram payload.
    /// Returns true when a valid quote was pushed to the ring buffer.
    bool handle_datagram(const std::uint8_t* data, std::size_t len);

    [[nodiscard]] const FeedHandlerStats& stats() const noexcept { return stats_; }
    [[nodiscard]] uint64_t expected_sequence() const noexcept { return expected_sequence_; }

private:
    SpscRingBuffer<Quote>& out_;
    uint64_t expected_sequence_;
    FeedHandlerStats stats_{};
};

} // namespace oms