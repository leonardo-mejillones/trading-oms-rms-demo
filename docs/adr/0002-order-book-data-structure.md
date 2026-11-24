# 2. Order Book Data Structure

Date: 2026-11-24

## Status

Accepted

## Context

The OMS/RMS demo needs an order book that enforces **price-time priority**. Orders at better prices must be matched first, and orders at the same price level must be matched in FIFO (first-in, first-out) order. The book must support:

- Adding resting limit orders.
- Querying best bid and best ask.
- Querying depth at a price level.

The data structure should be simple, correct, and easy to evolve as matching, cancellation, and modification are added in later stories.

## Decision

Use two sorted price-level maps, one per side:

- **Bids:** `std::map<Price, PriceLevel, std::greater<Price>>` so the highest bid is `begin()`.
- **Asks:** `std::map<Price, PriceLevel, std::less<Price>>` so the lowest ask is `begin()`.

Each `PriceLevel` holds a FIFO queue of resting orders using `std::deque<Order>`:

- New resting orders are appended with `push_back`.
- Future matching will consume from `front()` and remove with `pop_front`, preserving time priority.

Depth at a price level is computed as the sum of resting quantities in that level's queue.

## Alternatives Considered

### `std::map` + `std::list`
- **Pros:** Stable iterators/references for orders, making future cancel-by-id efficient if we store an index to the list iterator.
- **Cons:** Per-node heap allocations and poor cache locality.

### `std::unordered_map` + heap/index
- **Pros:** O(1) average lookup by price.
- **Cons:** Does not maintain sorted price order; best bid/ask requires extra bookkeeping or a heap. More complex and error-prone.

### Flat sorted vector / ladder
- **Pros:** Excellent cache locality for dense, narrow price ranges.
- **Cons:** Insert/erase in the middle is O(n); more implementation complexity. Better suited for performance-optimized production matching engines.

## Consequences

### Positive
- **Correctness:** `std::map` ordering directly enforces price priority; `std::deque` FIFO enforces time priority.
- **Simplicity:** The implementation is small, readable, and easy to test.
- **Extensibility:** Can evolve to include an order-id index for O(1) cancel lookup and switch level queues to `std::list` if stable iterators become necessary.

### Negative / Trade-offs
- `std::map` is node-based with weaker cache locality than contiguous structures.
- Depth queries currently aggregate by iterating the level queue; this is acceptable at demo scale and can be optimized with a cached aggregate quantity later.
- `std::deque` iterator invalidation rules require care if we later store iterators for cancellation; an order-id index should be designed deliberately when cancel is implemented.