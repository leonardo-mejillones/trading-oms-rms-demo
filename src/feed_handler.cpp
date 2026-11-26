#include "oms/feed_handler.hpp"

namespace oms {

FeedHandler::FeedHandler(SpscRingBuffer<Quote>& out, uint64_t expected_sequence)
    : out_(out), expected_sequence_(expected_sequence) {}

bool FeedHandler::handle_datagram(const std::uint8_t* data, std::size_t len) {
    const auto quote = decode_quote(data, len);
    if (!quote.has_value()) {
        ++stats_.invalid;
        return false;
    }

    ++stats_.received;

    if (quote->sequence < expected_sequence_) {
        // Late/duplicate datagram: ignore for downstream processing.
        ++stats_.duplicates;
        return false;
    }

    if (quote->sequence > expected_sequence_) {
        // Gap detected: one or more sequence numbers were skipped.
        ++stats_.gaps;
        stats_.missing += (quote->sequence - expected_sequence_);
        expected_sequence_ = quote->sequence + 1;
    } else {
        // In-order sequence.
        ++expected_sequence_;
    }

    // Push parsed quote to the SPSC ring buffer (producer side).
    if (!out_.try_push(*quote)) {
        ++stats_.dropped;
        return false;
    }

    ++stats_.pushed;
    return true;
}

} // namespace oms