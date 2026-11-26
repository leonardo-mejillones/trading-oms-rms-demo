# 3. Event Journal and Replay

Date: 2026-11-24

## Status

Accepted

## Context

The OMS/RMS demo needs crash-recovery capability. After a process restart or failure, operators must be able to rebuild in-memory state (including the order book) deterministically. The system already emits domain events through `EventSink`, making event-driven observability available.

We need a simple, append-only persistence model suitable for a demo that preserves determinism and supports state reconstruction.

## Alternatives Considered

### Journal domain output events only
- **Pros:** reuses events already emitted by the system.
- **Cons:** output events may not capture all inputs needed to deterministically rebuild state; replay semantics can be harder.

### Periodic full state snapshots
- **Pros:** fast recovery from a recent snapshot.
- **Cons:** snapshots alone miss changes between snapshots; requires snapshot/version management and still benefits from a log.

### Durable database/WAL from day one
- **Pros:** real durability and crash recovery guarantees.
- **Cons:** more operational and implementation complexity than needed for this educational in-memory demo.

## Decision

Introduce an append-only `EventJournal` that records order-processing inputs as journal entries:

- `Submit` (order)
- `Cancel` (order id)
- `Replace` (order id, new price, new quantity)

The journal is append-only: entries are only added, never mutated or removed. Replay applies entries in original order to a fresh `OrderBook` with a caller-provided `EventSink`, reproducing identical book state and deterministic behavior.

Domain output events continue to be emitted through `EventSink` during normal processing and replay. The journal captures the inputs required to rebuild state; emitted events remain the observable projection.

## Consequences

### Positive
- **Determinism:** same journal replayed in same order produces identical book state.
- **Recoverability:** in-memory order book can be rebuilt after crash by replaying the journal.
- **Auditability:** append-only log provides a clear sequence of operations.
- **Simplicity:** in-memory append-only vector is easy to test and reason about.

### Negative / Trade-offs
- In-memory journal is not durable across process loss by itself; persistent storage/file fsync can be added later.
- Journal grows without bound; snapshotting/compaction would be needed for long-running production systems.
- Replay time grows linearly with journal size.