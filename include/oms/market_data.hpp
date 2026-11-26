#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "oms/types.hpp"

namespace oms {

/// Synthetic top-of-book quote with a monotonic sequence number.
/// Sequence numbers are assigned by MarketDataSimulator and are intended to
/// help receivers detect gaps/duplicates in UDP streams.
struct Quote {
    uint64_t sequence{0};   // Monotonic sequence number assigned at generation time.
    std::string symbol;     // Instrument symbol (e.g. "AAPL").
    Price bid{};            // Best bid price in integer ticks.
    Price ask{};            // Best ask price in integer ticks.
    Quantity bid_qty{};     // Bid quantity.
    Quantity ask_qty{};     // Ask quantity.
};

// Fixed-size wire encoding for UDP quotes.
// Layout (native byte order, little/big endian depends on host):
//   uint64_t sequence
//   char symbol[16] (null-padded, truncated to 15 chars)
//   int64_t  bid_ticks
//   int64_t  ask_ticks
//   uint64_t bid_qty
//   uint64_t ask_qty
inline constexpr std::size_t kQuoteWireSymbolSize = 16;
inline constexpr std::size_t kQuoteWireSize = 8 + kQuoteWireSymbolSize + 8 + 8 + 8 + 8;

/// Serializes a Quote into the fixed-size wire format.
[[nodiscard]] std::vector<std::uint8_t> encode_quote(const Quote& quote);

/// Decodes a Quote from the fixed-size wire format.
/// Returns std::nullopt if the buffer is null or too small.
[[nodiscard]] std::optional<Quote> decode_quote(const std::uint8_t* data, std::size_t len);

/// Configuration for reproducible synthetic quote generation.
struct MarketDataConfig {
    uint64_t seed{1};             // RNG seed for deterministic output.
    std::string symbol{"AAPL"};   // Symbol stamped on generated quotes.
    int64_t min_price{90};        // Lower bound for generated bid ticks.
    int64_t max_price{110};       // Upper bound for generated ask ticks.
    uint64_t min_qty{1};          // Minimum generated quantity.
    uint64_t max_qty{20};         // Maximum generated quantity.
    uint64_t start_sequence{1};   // First sequence number to emit.
};

/// Generates reproducible synthetic top-of-book quotes.
/// Each call to next()/generate() assigns a monotonically increasing sequence
/// number, making output suitable for UDP gap-detection demos.
class MarketDataSimulator {
public:
    explicit MarketDataSimulator(MarketDataConfig config);

    /// Generates the next quote and advances the internal sequence number.
    [[nodiscard]] Quote next();

    /// Generates `count` quotes in order.
    [[nodiscard]] std::vector<Quote> generate(std::size_t count);

    /// Returns the next sequence number that will be assigned.
    [[nodiscard]] uint64_t sequence() const noexcept { return next_sequence_; }

private:
    MarketDataConfig config_;
    uint64_t next_sequence_;
    std::mt19937_64 rng_;
};

/// Simple UDP sender for serialized market data quotes.
/// The sender owns a datagram socket and a configured IPv4 destination.
/// It is not thread-safe; use from a single thread or synchronize externally.
class UdpMarketDataSender {
public:
    UdpMarketDataSender();
    ~UdpMarketDataSender();

    UdpMarketDataSender(const UdpMarketDataSender&) = delete;
    UdpMarketDataSender& operator=(const UdpMarketDataSender&) = delete;

    /// Configures the IPv4 destination. Creates the socket lazily on first call.
    /// Returns false if the address is invalid or socket creation fails.
    [[nodiscard]] bool configure(const std::string& host, uint16_t port);

    /// Encodes and sends one quote as a single UDP datagram.
    /// Returns false if not configured or if sendto fails/short-sends.
    [[nodiscard]] bool send(const Quote& quote);

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool configured() const noexcept { return configured_; }

private:
    int fd_{-1};                 // POSIX socket fd (-1 when not created).
    bool configured_{false};     // True after a successful configure().
    // Destination sockaddr stored as opaque bytes to keep socket headers out of this header.
    alignas(8) unsigned char dest_storage_[128]{};
    unsigned int dest_len_{0};
};

} // namespace oms