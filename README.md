# Trading OMS/RMS Demo

A C++20 demo application showcasing core **Order Management System (OMS)** and **Risk Management System (RMS)** concepts in trading software.

## Purpose

This project is a learning/demo codebase that evolves incrementally through user stories. It focuses on correctness, clear domain modeling, and testability for trading workflows such as order validation, event emission, order book management, matching, and cancellation.

## Implemented Features

### S0.1 - Project skeleton and build
- Reproducible C++20 CMake project.
- Standard directory layout: `include/`, `src/`, `tests/`, `bench/`, `docs/adr/`.
- Strict warnings for GCC/Clang/AppleClang: `-Wall -Wextra -Werror`.
- Minimal executable target: `trading_oms_rms_demo`.

### S0.2 - CI pipeline
- GitHub Actions workflow (`.github/workflows/ci.yml`) on `push` and `pull_request`.
- Build and test matrix: GCC and Clang, `Debug` and `Release`.
- Separate sanitizer job running tests with **ASan** and **UBSan**.

### S0.3 - Test harness
- GoogleTest integrated via CMake `FetchContent` (pinned version).
- Tests registered with CTest (`gtest_discover_tests`).
- Smoke test validates the test harness.

### S1.1 - Core domain types
- Strongly typed domain primitives in `include/oms/types.hpp`:
  - `Price` - integer ticks (`int64_t`), floating-point construction is not allowed.
  - `Quantity` - unsigned integer quantity (`uint64_t`).
  - `OrderId`, `AccountId` - typed identifiers.
  - `Side` (`Buy`, `Sell`), `OrderType` (`Limit`), `TimeInForce` (`GTC`, `IOC`).
- `Order` (`include/oms/order.hpp`, `src/order.cpp`) with validated construction:
  - non-zero order/account IDs,
  - positive quantity,
  - positive price for limit orders.
- ADR: [ADR-001 - Why Integer Prices](docs/adr/0001-why-integer-prices.md).

### S1.2 - Events and sink
- Domain events in `include/oms/events.hpp`:
  - `OrderAccepted`
  - `OrderRejected` (with reason)
  - `Fill` (trade/fill event with order id, price, quantity)
  - `OrderCanceled`
  - `CancelRejected` (with reason)
- `Event` as `std::variant` of supported event types.
- `EventSink` interface and `InMemoryEventSink` implementation in `include/oms/event_sink.hpp` for test observation.

### S2.1 - Order book structure
- `OrderBook` (`include/oms/order_book.hpp`, `src/order_book.cpp`) with price-time priority structure:
  - Bids: `std::map<Price, PriceLevel, std::greater<Price>>`.
  - Asks: `std::map<Price, PriceLevel, std::less<Price>>`.
  - Each price level is a FIFO `std::list<Order>`.
- Supports:
  - adding resting orders,
  - querying best bid/ask,
  - querying depth at a side/price level.
- ADR: [ADR-002 - Order Book Data Structure](docs/adr/0002-order-book-data-structure.md).

### S2.2 - Match incoming limit orders
- `OrderBook::submit` matches incoming limit orders against the opposite side.
- Enforces **price-time priority**.
- Executes trades at the **resting order price**.
- Supports **partial fills**.
- Remainder handling:
  - `GTC`: remaining quantity rests in the book.
  - `IOC`: remaining quantity is canceled (`OrderCanceled`).
- Emits domain events during processing (`OrderAccepted`, `Fill`, `OrderCanceled`).

### S2.3 - Cancel order
- `OrderBook::cancel` cancels a resting order by `OrderId`.
- Uses an `OrderId -> location` index for **O(1) lookup** (no full book scan).
- Cancels resting orders and publishes `OrderCanceled`.
- Unknown or already filled orders publish `CancelRejected` with a reason.

### S2.4 - Market orders
- `OrderType::Market` supported in the domain model.
- `OrderBook::submit` executes market orders immediately against best available prices on the opposite side.
- Market orders do not rest in the book; any unfilled remainder is canceled (`OrderCanceled`).
- Tests cover empty book and partial liquidity scenarios.

### S2.5 - Cancel/replace
- `OrderBook::replace` modifies a resting order by `OrderId`.
- **Price change or quantity increase:** loses time priority (re-queued at back of price level).
- **Quantity decrease at same price:** keeps time priority (updated in place).
- Unknown or already filled orders publish `CancelRejected`.

### S2.6 - Invariants and randomized tests
- Seeded randomized order flow tests compare `OrderBook` against a naive reference implementation.
- Checks invariants/properties:
  - book never crossed after matching,
  - FIFO/behavior matches reference event sequence,
  - quantity/depth state matches reference (conservation).

### S3.1 - Order state machine
- Explicit order lifecycle states: `New`, `Accepted`, `PartiallyFilled`, `Filled`, `Canceled`, `Rejected`.
- Explicit transition table and triggers (`Accept`, `Reject`, `PartialFill`, `Fill`, `Cancel`).
- Illegal transitions return an error result; allowed transitions return next state.
- Unit tests cover every allowed and illegal transition.

### S3.2 - Idempotency
- `ClientOrderId` added to domain model; `Oms` facade provides idempotent submit.
- Duplicate `(AccountId, ClientOrderId)` submissions replay the original result without reprocessing.
- Duplicate cancels are idempotent: canceling an already-canceled order returns `OrderCanceled` again instead of rejection.
- Canceling an already filled (not canceled) order remains rejected.

### S3.3 - Validation
- `RejectReason` enum with distinct reasons: `InvalidQuantity`, `InvalidPrice`, `InvalidTickSize`, `InvalidLotSize`, `UnknownSymbol`.
- `validate_order` validates raw `OrderRequest` (and `Order`) against instrument specs (tick size, lot size, known symbol).
- Early OMS validation checks positive quantity/price, tick/lot alignment, and known symbol.

### S3.4 - Journal and replay
- Append-only `EventJournal` records order-processing inputs (`Submit`, `Cancel`, `Replace`).
- `replay` applies entries in original order to a fresh `OrderBook`, reproducing identical state/events (determinism test).
- ADR: [ADR-003 - Event Journal and Replay](docs/adr/0003-event-journal-and-replay.md).

### S4.1 - Risk check pipeline
- Composable pre-trade `RiskCheck` interface and `RiskCheckPipeline`.
- Each check returns pass or reject(reason); pipeline short-circuits on first rejection.
- `Oms` runs risk pipeline before submitting to the book; rejected orders emit `OrderRejected` and do not reach matching.

### S4.2 - Size and notional limits
- `MaxOrderSizeCheck`: rejects orders exceeding configured max quantity.
- `MaxNotionalCheck`: rejects limit orders exceeding configured max notional (`price ticks * quantity`).
- Designed as composable `RiskCheck` implementations to block fat-finger errors pre-trade.

### S4.3 - Position tracking and limits
- `PositionTracker` tracks net positions per account/symbol from fills and tracks open-order exposure.
- `PositionLimitCheck` enforces max long/short limits considering worst-case exposure including open orders.
- `Oms` integrates tracker updates (register/apply events) so positions and exposure stay consistent.
- Tests cover long and short limits and fill-driven position updates.

### S4.4 - Order rate limit
- `RateLimitCheck` enforces a per-account token-bucket rate limit to throttle runaway algorithms.
- Uses an injectable clock so rate-limit behavior is deterministic in tests.
- Designed as a composable `RiskCheck` that rejects orders with `rate limit exceeded` when exhausted.

### S4.5 - Account halt / kill switch
- `Oms` supports halting/resuming accounts (`halt_account`, `resume_account`, `is_account_halted`).
- New orders and replaces for a halted account are rejected with `account halted`.
- Cancels remain allowed so risk/operators can reduce exposure while an account is halted.

### S5.1 - Matching engine facade
- `MatchingEngine` provides a single entry point that processes `EngineCommand`s (`Submit`, `Cancel`, `Replace`) in order.
- A single-threaded sequencer applies validation, risk, then matching; the same input sequence produces deterministic output.
- ADR: [ADR-004 - Single-Writer Design](docs/adr/0004-single-writer-design.md).

### S5.2 - SPSC ring buffer
- `SpscRingBuffer<T>` provides a bounded lock-free single-producer/single-consumer queue for gateway-to-engine handoff.
- `try_push`/`try_pop` are wait-free; memory ordering is documented in the header (acquire/release on head/tail indices).
- Stress-tested with concurrent producer/consumer tests and a dedicated ThreadSanitizer CI job.

### S6.1 - Order flow generator
- `OrderFlowGenerator` produces a reproducible, seeded synthetic workload as `EngineCommand`s.
- Configurable mix includes passive new limit orders, aggressive crossing limit orders, and cancels.

### S6.2 - Latency measurement
- `bench/latency_bench.cpp` measures per-`MatchingEngine::process` command latency on a reproducible seeded workload.
- Reports warm-up phase and percentile latencies: p50, p90, p99, p99.9, max.
- Results below are honest measurements from this machine/build (not normalized or cherry-picked).

**Environment**
- Hardware: Apple M5 Pro (Mac17,9, arm64)
- OS: Darwin 27.0.0 (macOS)
- Compiler: Apple clang 21.0.0 (clang-2100.4.1.1), target arm64-apple-darwin27.0.0
- Build: `Release` (`-O3` via CMake Release defaults), `CMAKE_CXX_STANDARD=20`

**Command**
```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target latency_bench -j4
./build-release/bench/latency_bench
```

**Results (seed=42, warm-up=5000, measured=50000)**

| Metric | ns |
|---|---:|
| p50 | 250.0 |
| p90 | 542.0 |
| p99 | 2375.0 |
| p99.9 | 8792.2 |
| max | 3491291.0 |

## Project Structure

```text
.
├── .github/workflows/
│   └── ci.yml                        # CI: build/test matrix + ASan/UBSan job
├── bench/
│   ├── CMakeLists.txt                # Benchmark targets
│   └── latency_bench.cpp             # Per-command latency benchmark
├── docs/
│   └── adr/
│       ├── 0001-why-integer-prices.md
│       ├── 0002-order-book-data-structure.md
│       ├── 0003-event-journal-and-replay.md
│       └── 0004-single-writer-design.md
├── include/
│   └── oms/
│       ├── event_sink.hpp            # EventSink interface + in-memory sink
│       ├── events.hpp                # Domain events and Event variant
│       ├── journal.hpp               # Append-only event journal and replay
│       ├── matching_engine.hpp       # Single-entry sequencer: validation, risk, matching
│       ├── oms.hpp                   # OMS facade with idempotent submission
│       ├── order.hpp                 # Order model with validation
│       ├── order_book.hpp            # Order book API
│       ├── order_flow_generator.hpp   # Seeded synthetic workload generator
│       ├── order_state.hpp           # Order lifecycle state machine
│       ├── position.hpp              # Position tracking and position limits
│       ├── risk.hpp                  # Composable pre-trade risk checks
│       ├── spsc_ring_buffer.hpp      # Bounded lock-free SPSC queue
│       ├── types.hpp                 # Strong domain primitives/enums
│       └── validation.hpp            # Order validation and reject reasons
├── src/
│   ├── journal.cpp                   # Journal append/replay implementation
│   ├── main.cpp                      # Demo executable entry point
│   ├── matching_engine.cpp           # Matching engine facade/sequencer
│   ├── oms.cpp                       # OMS idempotency facade
│   ├── order.cpp                     # Order validation/formatting
│   ├── order_book.cpp                # Add/match/cancel/replace/depth logic
│   ├── order_flow_generator.cpp      # Synthetic workload generation
│   ├── order_state.cpp               # Order lifecycle transition table
│   └── validation.cpp                # Order validation rules
├── tests/
│   ├── cancel_test.cpp
│   ├── events_test.cpp
│   ├── idempotency_test.cpp
│   ├── invariants_test.cpp
│   ├── journal_test.cpp
│   ├── market_order_test.cpp
│   ├── matching_test.cpp
│   ├── matching_engine_test.cpp
│   ├── order_book_test.cpp
│   ├── order_flow_generator_test.cpp
│   ├── order_state_test.cpp
│   ├── position_test.cpp
│   ├── rate_limit_test.cpp
│   ├── account_halt_test.cpp
│   ├── replace_test.cpp
│   ├── risk_limits_test.cpp
│   ├── risk_test.cpp
│   ├── spsc_ring_buffer_test.cpp
│   ├── smoke_test.cpp
│   ├── types_test.cpp
│   └── validation_test.cpp
├── CMakeLists.txt
└── README.md
```

## Requirements

- CMake >= 3.20
- A C++20-compatible compiler:
  - GCC
  - Clang / AppleClang

## Build

Configure and build from the repository root:

```bash
cmake -S . -B build
cmake --build build
```

Run the demo executable:

```bash
./build/trading_oms_rms_demo
```

## Test

Run all tests with CTest:

```bash
ctest --test-dir build --output-on-failure
```

Current test suites cover:
- smoke/harness sanity,
- domain types and order validation,
- events and in-memory event sink,
- order book structure/depth/best prices,
- matching behavior (no match, exact, multi-level sweep, partial fill, GTC/IOC),
- market order behavior (empty book, partial liquidity, best available prices),
- cancel/replace behavior (priority loss/keep rules),
- cancellation behavior (success, unknown/filled, FIFO preservation),
- seeded randomized invariants against a naive reference implementation,
- order lifecycle state machine transitions (allowed and illegal),
- idempotent submission and duplicate cancel behavior,
- order validation with distinct reject reasons,
- append-only journal replay determinism,
- composable pre-trade risk checks with short-circuit rejection,
- max order size and max notional fat-finger limits,
- position tracking and per-account/symbol position limits (long/short),
- per-account token-bucket order rate limiting with deterministic clock injection,
- account halt/kill switch (new orders/replaces rejected, cancels allowed),
- matching engine facade sequencing commands with validation, risk, then matching deterministically,
- bounded SPSC ring buffer with wait-free producer/consumer operations and concurrent stress tests,
- reproducible seeded order-flow generation with passive, aggressive, and cancel commands.

## Continuous Integration

GitHub Actions CI (`.github/workflows/ci.yml`) runs on every push and pull request:

- **Build & Test:** GCC and Clang, in both `Debug` and `Release` configurations.
- **Sanitizers:** a separate job runs tests with **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)**.

## Architecture Decision Records

- [ADR-001: Why Integer Prices](docs/adr/0001-why-integer-prices.md)
- [ADR-002: Order Book Data Structure](docs/adr/0002-order-book-data-structure.md)
- [ADR-003: Event Journal and Replay](docs/adr/0003-event-journal-and-replay.md)
- [ADR-004: Single-Writer Design](docs/adr/0004-single-writer-design.md)

## Development Notes

- The build is configured for C++20 (`CMAKE_CXX_STANDARD 20`, no compiler extensions).
- Compiler warnings are treated as errors for GCC/Clang/AppleClang: `-Wall -Wextra -Werror`.
- The project is developed incrementally, story by story.
