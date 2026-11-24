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

## Project Structure

```text
.
├── .github/workflows/
│   └── ci.yml                        # CI: build/test matrix + ASan/UBSan job
├── bench/                            # Benchmarks (placeholder)
├── docs/
│   └── adr/
│       ├── 0001-why-integer-prices.md
│       └── 0002-order-book-data-structure.md
├── include/
│   └── oms/
│       ├── event_sink.hpp            # EventSink interface + in-memory sink
│       ├── events.hpp                # Domain events and Event variant
│       ├── oms.hpp                   # OMS facade with idempotent submission
│       ├── order.hpp                 # Order model with validation
│       ├── order_book.hpp            # Order book API
│       ├── order_state.hpp           # Order lifecycle state machine
│       ├── types.hpp                 # Strong domain primitives/enums
│       └── validation.hpp            # Order validation and reject reasons
├── src/
│   ├── main.cpp                      # Demo executable entry point
│   ├── oms.cpp                       # OMS idempotency facade
│   ├── order.cpp                     # Order validation/formatting
│   ├── order_book.cpp                # Add/match/cancel/replace/depth logic
│   ├── order_state.cpp               # Order lifecycle transition table
│   └── validation.cpp                # Order validation rules
├── tests/
│   ├── cancel_test.cpp
│   ├── events_test.cpp
│   ├── idempotency_test.cpp
│   ├── invariants_test.cpp
│   ├── market_order_test.cpp
│   ├── matching_test.cpp
│   ├── order_book_test.cpp
│   ├── order_state_test.cpp
│   ├── replace_test.cpp
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
- order validation with distinct reject reasons.

## Continuous Integration

GitHub Actions CI (`.github/workflows/ci.yml`) runs on every push and pull request:

- **Build & Test:** GCC and Clang, in both `Debug` and `Release` configurations.
- **Sanitizers:** a separate job runs tests with **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)**.

## Architecture Decision Records

- [ADR-001: Why Integer Prices](docs/adr/0001-why-integer-prices.md)
- [ADR-002: Order Book Data Structure](docs/adr/0002-order-book-data-structure.md)

## Development Notes

- The build is configured for C++20 (`CMAKE_CXX_STANDARD 20`, no compiler extensions).
- Compiler warnings are treated as errors for GCC/Clang/AppleClang: `-Wall -Wextra -Werror`.
- The project is developed incrementally, story by story.
