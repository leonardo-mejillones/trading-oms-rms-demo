# 4. Single-Writer Design

Date: 2026-11-24

## Status

Accepted

## Context

The matching engine must process commands deterministically and preserve order book invariants. Trading systems commonly choose between a single-writer/sequenced design and multi-threaded designs with locks or lock-free structures. This demo needs a clear, testable processing model for command ordering, validation, risk checks, and matching.

## Alternatives Considered

### Coarse-grained locks around the book/engine
- **Pros:** simpler to add concurrency incrementally.
- **Cons:** lock contention, deadlock risk, and harder-to-reproduce behavior under load.

### Fine-grained or lock-free multi-threaded matching
- **Pros:** potential throughput/scaling benefits.
- **Cons:** significantly higher complexity; difficult to guarantee determinism and invariants in a demo.

### Actor/mailbox model per symbol
- **Pros:** good scaling path while preserving single-writer semantics per shard.
- **Cons:** more infrastructure than needed now; can be adopted later as a scaling strategy.

## Decision

Adopt a **single-writer, single-threaded sequencer** as the matching engine entry point. Commands (`Submit`, `Cancel`, `Replace`) are processed in arrival order by one thread through a single facade (`MatchingEngine`). For each command, the engine applies validation, then risk checks, then matching/book mutation.

No locks are used in the core matching path. Determinism comes from explicit command ordering and single-threaded mutation of engine state.

## Consequences

### Positive
- **Determinism:** the same command sequence produces the same events and final book state.
- **Simplicity:** no lock ordering, deadlock avoidance, or concurrent mutation bugs in the core path.
- **Invariant safety:** order book and OMS state are mutated by one writer, making invariants easier to maintain.
- **Testability:** deterministic sequencing simplifies replay and regression tests.

### Negative / Trade-offs
- Throughput is limited by a single core for the core matching path.
- Latency spikes in command processing can block later commands.
- Scaling to multi-symbol/high-throughput workloads requires partitioning (e.g., one sequencer per symbol/shard) or additional concurrency architecture.