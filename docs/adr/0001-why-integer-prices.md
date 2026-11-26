# 1. Why Integer Prices (Tick-Based Representation)

Date: 2026-11-22

## Status

Accepted

## Context

Financial trading systems require deterministic, bit-exact arithmetic. Prices in market data feeds, exchange matching engines, and order management systems (OMS / RMS) are quoted in discrete increments known as "ticks" (e.g., $0.01 for equities, 0.25 index points for futures).

Using standard IEEE-754 binary floating-point numbers (`float`, `double`) introduces significant risks:
1. **Binary Representation Errors:** Common decimal values like `0.1` or `0.01` cannot be represented exactly in IEEE-754 binary floating-point (`0.1` becomes `0.10000000000000000555...`).
2. **Non-Associative Arithmetic:** `(a + b) + c != a + (b + c)` due to rounding, leading to nondeterministic order matching and PnL discrepancies.
3. **Imprecise Comparisons:** Order book priority requires exact equality and strict ordering (e.g., `price1 == price2` or `price1 >= price2`). Floating-point comparison with epsilon thresholds is fragile, slow, and dangerous in matching engines and risk checks.
4. **Regulatory and Audit Compliance:** Financial regulators require exact accounting of execution prices, trade notionals, and commissions. Floating-point drift violates auditability.

## Alternatives Considered

### IEEE-754 `double`/`float`
- **Pros:** native language support, convenient for quick prototypes.
- **Cons:** binary representation and rounding errors make exact price comparison and accounting unsafe.

### Fixed-point / decimal libraries
- **Pros:** can represent decimal fractions directly.
- **Cons:** adds dependency or implementation complexity; tick-based integers map naturally to exchange price increments.

### Raw `int64_t` without a strong type
- **Pros:** simple and efficient.
- **Cons:** easy to mix prices with quantities/IDs; loses compile-time safety.

## Decision

We represent all prices using integer ticks (`int64_t`) wrapped in a strongly typed `Price` primitive:
- No floating-point types (`float`, `double`) are permitted in core domain models.
- Float and double constructors/conversions are explicitly `= delete`d to trigger compile-time errors if floating-point variables are accidentally passed.
- Conversion to/from human-readable decimals or external protocol formats occurs strictly at system boundaries (e.g., serialization / deserialization layers).

## Consequences

### Positive
- **Determinism:** Bit-exact calculations across all hardware architectures, compilers, and optimization levels.
- **Performance:** Integer arithmetic and comparisons are faster and avoid floating-point CPU pipeline stalls.
- **Safety:** Compile-time prevention of accidental floating-point operations and unit mismatches.
- **Simplicity:** Matching engine price levels map directly to integer hash maps or array indices.

### Negative / Trade-offs
- Display layers must explicitly convert integer ticks to decimal strings given an instrument's tick size.
- Cross-instrument fractional operations (e.g., complex pricing models) must handle scale factors explicitly.
