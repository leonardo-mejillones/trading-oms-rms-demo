#include "oms/market_data.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace oms {

namespace {

template <typename T>
void write_pod(std::vector<std::uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
bool read_pod(const std::uint8_t* data, std::size_t len, std::size_t& offset, T& out) {
    if (offset + sizeof(T) > len) {
        return false;
    }
    std::memcpy(&out, data + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

} // namespace

// Encodes a Quote into the fixed-size wire format.
// Numeric fields are serialized in native byte order via memcpy.
// Symbol is truncated to kQuoteWireSymbolSize-1 and null-padded.
std::vector<std::uint8_t> encode_quote(const Quote& quote) {
    std::vector<std::uint8_t> out;
    out.reserve(kQuoteWireSize);

    write_pod(out, quote.sequence);

    char symbol[kQuoteWireSymbolSize]{};
    const std::size_t copy_len = std::min(quote.symbol.size(), kQuoteWireSymbolSize - 1);
    std::memcpy(symbol, quote.symbol.data(), copy_len);
    out.insert(out.end(), symbol, symbol + kQuoteWireSymbolSize);

    write_pod(out, quote.bid.ticks);
    write_pod(out, quote.ask.ticks);
    write_pod(out, quote.bid_qty.value);
    write_pod(out, quote.ask_qty.value);

    return out;
}

// Decodes a Quote from the fixed-size wire format.
// Returns nullopt when the buffer is null or too small.
std::optional<Quote> decode_quote(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len < kQuoteWireSize) {
        return std::nullopt;
    }

    Quote quote;
    std::size_t offset = 0;
    if (!read_pod(data, len, offset, quote.sequence)) return std::nullopt;

    char symbol[kQuoteWireSymbolSize]{};
    std::memcpy(symbol, data + offset, kQuoteWireSymbolSize);
    offset += kQuoteWireSymbolSize;
    symbol[kQuoteWireSymbolSize - 1] = '\0'; // Ensure null termination.
    quote.symbol = std::string(symbol);

    int64_t bid_ticks = 0;
    int64_t ask_ticks = 0;
    uint64_t bid_qty = 0;
    uint64_t ask_qty = 0;
    if (!read_pod(data, len, offset, bid_ticks)) return std::nullopt;
    if (!read_pod(data, len, offset, ask_ticks)) return std::nullopt;
    if (!read_pod(data, len, offset, bid_qty)) return std::nullopt;
    if (!read_pod(data, len, offset, ask_qty)) return std::nullopt;

    quote.bid = Price{bid_ticks};
    quote.ask = Price{ask_ticks};
    quote.bid_qty = Quantity{bid_qty};
    quote.ask_qty = Quantity{ask_qty};
    return quote;
}

MarketDataSimulator::MarketDataSimulator(MarketDataConfig config)
    : config_(std::move(config)), next_sequence_(config_.start_sequence), rng_(config_.seed) {}

Quote MarketDataSimulator::next() {
    // Keep bid below max so ask can be >= bid+1. The spread is a small synthetic value.
    std::uniform_int_distribution<int64_t> price_dist(config_.min_price, config_.max_price - 1);
    std::uniform_int_distribution<uint64_t> qty_dist(config_.min_qty, config_.max_qty);
    std::uniform_int_distribution<int64_t> spread_dist(1, 3);

    const int64_t bid = price_dist(rng_);
    const int64_t spread = spread_dist(rng_);
    const int64_t ask = std::min(bid + spread, config_.max_price);

    Quote quote;
    quote.sequence = next_sequence_++; // Monotonic sequence for gap detection.
    quote.symbol = config_.symbol;
    quote.bid = Price{bid};
    quote.ask = Price{std::max(ask, bid + 1)}; // Guarantee ask > bid.
    quote.bid_qty = Quantity{qty_dist(rng_)};
    quote.ask_qty = Quantity{qty_dist(rng_)};
    return quote;
}

std::vector<Quote> MarketDataSimulator::generate(std::size_t count) {
    std::vector<Quote> quotes;
    quotes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        quotes.push_back(next());
    }
    return quotes;
}

UdpMarketDataSender::UdpMarketDataSender() = default;

UdpMarketDataSender::~UdpMarketDataSender() {
#if defined(_WIN32)
    if (fd_ != -1) {
        ::closesocket(static_cast<SOCKET>(fd_));
    }
#else
    if (fd_ != -1) {
        ::close(fd_);
    }
#endif
}

bool UdpMarketDataSender::configure(const std::string& host, uint16_t port) {
#if defined(_WIN32)
    // Initialize Winsock once per configure call (simple demo behavior).
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
#endif
    if (fd_ == -1) {
        // Create an IPv4 UDP datagram socket lazily.
#if defined(_WIN32)
        fd_ = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
#else
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
#endif
        if (fd_ == -1) {
            return false;
        }
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        configured_ = false;
        return false;
    }

    // Store destination as opaque bytes so the header does not expose socket headers.
    static_assert(sizeof(dest_storage_) >= sizeof(sockaddr_in));
    std::memcpy(dest_storage_, &addr, sizeof(addr));
    dest_len_ = static_cast<unsigned int>(sizeof(addr));
    configured_ = true;
    return true;
}

bool UdpMarketDataSender::send(const Quote& quote) {
    if (!configured_ || fd_ == -1) {
        return false;
    }
    const auto payload = encode_quote(quote);
    const auto* dest = reinterpret_cast<const sockaddr*>(dest_storage_);
#if defined(_WIN32)
    const int sent = ::sendto(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(payload.data()),
                              static_cast<int>(payload.size()), 0, dest, static_cast<int>(dest_len_));
#else
    const auto sent = ::sendto(fd_, payload.data(), payload.size(), 0, dest, dest_len_);
#endif
    // Treat short sends as failure so callers can detect delivery issues.
    return sent == static_cast<decltype(sent)>(payload.size());
}

} // namespace oms