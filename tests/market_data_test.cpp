#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "oms/market_data.hpp"

using namespace oms;

namespace {

int close_socket(int fd) {
#if defined(_WIN32)
    return ::closesocket(static_cast<SOCKET>(fd));
#else
    return ::close(fd);
#endif
}

} // namespace

TEST(MarketDataTest, SimulatorGeneratesMonotonicSequenceNumbers) {
    MarketDataConfig config;
    config.seed = 42;
    config.start_sequence = 100;
    MarketDataSimulator sim(config);

    const auto quotes = sim.generate(5);
    ASSERT_EQ(quotes.size(), 5u);
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        EXPECT_EQ(quotes[i].sequence, 100u + i);
        EXPECT_EQ(quotes[i].symbol, "AAPL");
        EXPECT_LT(quotes[i].bid.ticks, quotes[i].ask.ticks);
        EXPECT_GT(quotes[i].bid_qty.value, 0u);
        EXPECT_GT(quotes[i].ask_qty.value, 0u);
    }
    EXPECT_EQ(sim.sequence(), 105u);
}

TEST(MarketDataTest, SimulatorIsDeterministicForSameSeed) {
    MarketDataConfig config;
    config.seed = 7;
    config.symbol = "AAPL";

    MarketDataSimulator a(config);
    MarketDataSimulator b(config);

    const auto qa = a.generate(20);
    const auto qb = b.generate(20);
    ASSERT_EQ(qa.size(), qb.size());
    for (std::size_t i = 0; i < qa.size(); ++i) {
        EXPECT_EQ(qa[i].sequence, qb[i].sequence);
        EXPECT_EQ(qa[i].symbol, qb[i].symbol);
        EXPECT_EQ(qa[i].bid, qb[i].bid);
        EXPECT_EQ(qa[i].ask, qb[i].ask);
        EXPECT_EQ(qa[i].bid_qty, qb[i].bid_qty);
        EXPECT_EQ(qa[i].ask_qty, qb[i].ask_qty);
    }
}

TEST(MarketDataTest, EncodeDecodeRoundTrip) {
    Quote quote;
    quote.sequence = 1234;
    quote.symbol = "AAPL";
    quote.bid = Price{100};
    quote.ask = Price{101};
    quote.bid_qty = Quantity{10};
    quote.ask_qty = Quantity{20};

    const auto bytes = encode_quote(quote);
    ASSERT_EQ(bytes.size(), kQuoteWireSize);

    const auto decoded = decode_quote(bytes.data(), bytes.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sequence, quote.sequence);
    EXPECT_EQ(decoded->symbol, quote.symbol);
    EXPECT_EQ(decoded->bid, quote.bid);
    EXPECT_EQ(decoded->ask, quote.ask);
    EXPECT_EQ(decoded->bid_qty, quote.bid_qty);
    EXPECT_EQ(decoded->ask_qty, quote.ask_qty);
}

TEST(MarketDataTest, UdpSenderSendsQuotesToLoopbackReceiver) {
#if defined(_WIN32)
    WSADATA wsa{};
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsa), 0);
#endif

    const int recv_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(recv_fd, 0);

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(0); // ephemeral
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(recv_fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)), 0);

    sockaddr_in actual{};
    socklen_t actual_len = sizeof(actual);
    ASSERT_EQ(::getsockname(recv_fd, reinterpret_cast<sockaddr*>(&actual), &actual_len), 0);
    const uint16_t port = ntohs(actual.sin_port);

    // Set receive timeout to avoid hanging if send fails.
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
#if defined(_WIN32)
    DWORD timeout_ms = 1000;
    ASSERT_EQ(::setsockopt(recv_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)), 0);
#else
    ASSERT_EQ(::setsockopt(recv_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0);
#endif

    UdpMarketDataSender sender;
    ASSERT_TRUE(sender.configure("127.0.0.1", port));

    Quote quote;
    quote.sequence = 7;
    quote.symbol = "AAPL";
    quote.bid = Price{99};
    quote.ask = Price{101};
    quote.bid_qty = Quantity{5};
    quote.ask_qty = Quantity{6};
    ASSERT_TRUE(sender.send(quote));

    std::array<std::uint8_t, 256> buf{};
    sockaddr_in src{};
    socklen_t src_len = sizeof(src);
    const auto received = ::recvfrom(recv_fd, reinterpret_cast<char*>(buf.data()), buf.size(), 0,
                                     reinterpret_cast<sockaddr*>(&src), &src_len);
    ASSERT_GT(received, 0);

    const auto decoded = decode_quote(buf.data(), static_cast<std::size_t>(received));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sequence, 7u);
    EXPECT_EQ(decoded->symbol, "AAPL");
    EXPECT_EQ(decoded->bid, Price{99});
    EXPECT_EQ(decoded->ask, Price{101});

    close_socket(recv_fd);
#if defined(_WIN32)
    WSACleanup();
#endif
}