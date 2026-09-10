# ADR-007 — The server owns a `Simulation` it ticks on a schedule, not a `World` it ticks on a timer

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10. The seam itself was decided in build session MVP-01 and is kept; the interface and the schedule are rewritten here for the 4X (owner decision: the pre-4X record is rewritten in place, `Design/README.md` §4).
**Supersedes:** —

---

## Context

`AGENTS.md` §2 says `GameLogic` is referenced by the executables and by nothing else, and that
`NeuronServer` references `NeuronCore` and nothing more. So the engine's server cannot name
`Frontier::MatchState`. The tree resolves this with an abstract `Neuron::Simulation` declared in
`NeuronCore` — the one library both halves share — which `Frontier::World` implements and the
executable hands to `Neuron::Session`. That shape is right and is kept; `NeuronServerTests`
already proves its worth with a counting simulation that tests the server without a game.

What is wrong is everything the interface and the session say about time. `Simulation` has
`ApplyOrder(MoveToOrder)`, `Tick()` and `Snapshot() -> ShipState`; `Session` runs a thread that
ticks it twenty times a second against `steady_clock`. The 4X ticks four times a day at fixed UTC
times, its orders are per-seat and editable until the lock, its snapshot is per seat (ADR-005),
and the test plan needs the schedule compressed to one hour for Phase 0 and six for Phase 1.

## Options considered

### A. Let `NeuronServer` reference `GameLogic`

The least code. It deletes the rule that makes the engine an engine, and makes a test of the
schedule fail when a combat rule changes. Rejected in MVP-01 for the same reasons; nothing has
changed.

### B. Make `Session` a template on the simulation type

No virtual calls; the whole session in a header; every holder of a session a template too. The
tail wagging the dog, as before.

### C. Keep the abstract `Simulation`, widen it, and give `Session` a schedule

The seam stays where it is. The interface grows the questions the 4X server has to ask, and the
session's timer becomes a schedule that is match data.

## Decision

**C.** `Neuron::Simulation` is widened to what the server needs and nothing it does not:

- `Submit(seat, order) -> Verdict` and `Withdraw(seat, orderId) -> Verdict`, applied to the
  seat's order book for the next lock. The book is editable until the lock and hidden from every
  other seat.
- `OrderBook(seat)`, so a client can read back what it committed.
- `ResolveTick()`, the lock and the six phases of ADR-004, once per scheduled tick.
- `Snapshot(seat)` and `Digest(seat)`, the two records of ADR-005.
- `Preview(seat, request)`, the server-side engagement preview of ADR-013.
- `Save(bytes)` and `Load(bytes)`, the persistence of ADR-012.
- `SeatCount()` and `IsOver()`.

Order, snapshot, digest and preview records are `NeuronCore` wire records (ADR-006). They are the
one place the engine carries game nouns, as the MVP-01 record already admitted for its two; the
engine still does not know what any of them mean.

**`Session` ticks on a `TickSchedule`, and the schedule is match data.** A schedule is a first
tick time in UTC and a fixed interval; the one-pager's four a day is an interval of six hours,
Phase 0's compressed clock is one hour. The session reads `system_clock` — wall time, in UTC —
only to decide when the next lock is, sleeps until it, calls `ResolveTick()`, persists, and sends
every seat its snapshot and digest. Between ticks it serves requests. The wall clock never enters
the simulation: R16 is untouched, and the resolver cannot tell which schedule it is on.

**`ResolveNow()` exists on `Session`** for the harness and the tests, and for nothing else. It
locks and resolves out of schedule. It is not reachable from a client message.

**The tick number in the snapshot is what the client trusts**, not the wall clock. A client whose
clock is wrong sees the right tick with a wrong countdown, which is recoverable; the reverse is
not.

## Consequences

**What this makes easy.** `NeuronServerTests` still tests the schedule, the order books, the
per-seat send and persistence with a counting simulation, and `GameLogicTests` still tests every
rule without a thread. The compressed clock of Phase 0 is a schedule and nothing else. The
harness of ADR-011 drives the same `Session` with `ResolveNow()`.

**What this makes hard.** The interface is wider, and every addition is a change to a header both
halves compile against. That friction is the good kind: it is the moment to ask whether the server
should be asking.

**What it costs.** Virtual calls at four a day, which is nothing; and an interface that has to be
learned.

**What it forecloses.** Nothing yet. If one server ever hosts many matches, `Session` becomes
one of many and this interface is unchanged.

## What this changes elsewhere

- **AGENTS.md:** §2's dependency graph is what this preserves. R16 gains the sentence that the
  schedule lives in `NeuronServer`.
- **Design/:** ADR-004 is what `ResolveTick()` runs; ADR-005 what `Snapshot` and `Digest` return;
  ADR-011 which process owns a `Session`; ADR-012 what `Save` and `Load` are for.
  `Design/Plans/MVP-02-TheLoop.md` steps 3 and 4.
- **Code:** nothing yet. `NeuronCore/Simulation.h` and `NeuronServer/Session.{h,cpp}` are
  rewritten by the plan; their current comments describe the MVP-01 interface.

## Open questions

Whether a seat that never connected before the first lock should be treated as absent from tick
one for the custodian rule. The one-pager counts absence in ticks; the plan treats the first lock
as tick one and lets the rule run.
