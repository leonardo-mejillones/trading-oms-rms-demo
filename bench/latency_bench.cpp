#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "oms/matching_engine.hpp"
#include "oms/order_flow_generator.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Stats {
    double p50_ns{0.0};
    double p90_ns{0.0};
    double p99_ns{0.0};
    double p999_ns{0.0};
    double max_ns{0.0};
};

double percentile_ns(std::vector<double> sorted_ns, double p) {
    if (sorted_ns.empty()) {
        return 0.0;
    }
    if (p <= 0.0) {
        return sorted_ns.front();
    }
    if (p >= 100.0) {
        return sorted_ns.back();
    }
    const double rank = (p / 100.0) * static_cast<double>(sorted_ns.size() - 1);
    const auto lower = static_cast<std::size_t>(rank);
    const auto upper = std::min(lower + 1, sorted_ns.size() - 1);
    const double weight = rank - static_cast<double>(lower);
    return sorted_ns[lower] * (1.0 - weight) + sorted_ns[upper] * weight;
}

Stats compute_stats(std::vector<double> samples_ns) {
    std::sort(samples_ns.begin(), samples_ns.end());
    Stats stats;
    stats.p50_ns = percentile_ns(samples_ns, 50.0);
    stats.p90_ns = percentile_ns(samples_ns, 90.0);
    stats.p99_ns = percentile_ns(samples_ns, 99.0);
    stats.p999_ns = percentile_ns(samples_ns, 99.9);
    stats.max_ns = samples_ns.empty() ? 0.0 : samples_ns.back();
    return stats;
}

} // namespace

int main() {
    constexpr std::size_t kWarmup = 5000;
    constexpr std::size_t kMeasured = 50000;
    constexpr uint64_t kSeed = 42;

    oms::OrderFlowConfig config;
    config.seed = kSeed;
    config.count = kWarmup + kMeasured;
    config.symbol = "AAPL";
    config.account = oms::AccountId{1};
    config.min_price = 90;
    config.max_price = 110;
    config.min_qty = 1;
    config.max_qty = 20;

    oms::OrderFlowGenerator generator;
    const auto commands = generator.generate(config);

    oms::MatchingEngine engine({{"AAPL", oms::InstrumentSpec{"AAPL", 1, 1}}});
    oms::InMemoryEventSink sink;
    sink.reserve(commands.size() * 4); // reduce reallocations in the measurement sink

    // Warm-up phase: process commands to warm caches/paths; do not record.
    for (std::size_t i = 0; i < kWarmup && i < commands.size(); ++i) {
        engine.process(commands[i], sink);
    }

    std::vector<double> latencies_ns;
    latencies_ns.reserve(kMeasured);

    // Measured phase: record per-command latency.
    for (std::size_t i = kWarmup; i < commands.size(); ++i) {
        const auto start = Clock::now();
        engine.process(commands[i], sink);
        const auto end = Clock::now();
        const std::chrono::duration<double, std::nano> elapsed = end - start;
        latencies_ns.push_back(elapsed.count());
    }

    const Stats stats = compute_stats(std::move(latencies_ns));

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Latency benchmark (per MatchingEngine::process command)\n";
    std::cout << "Warm-up commands: " << kWarmup << "\n";
    std::cout << "Measured commands: " << kMeasured << "\n";
    std::cout << "Seed: " << kSeed << "\n\n";
    std::cout << "| Metric | ns |\n";
    std::cout << "|---|---:|\n";
    std::cout << "| p50 | " << stats.p50_ns << " |\n";
    std::cout << "| p90 | " << stats.p90_ns << " |\n";
    std::cout << "| p99 | " << stats.p99_ns << " |\n";
    std::cout << "| p99.9 | " << stats.p999_ns << " |\n";
    std::cout << "| max | " << stats.max_ns << " |\n";

    return 0;
}