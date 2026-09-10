# ADR-004 — The world is a graph, and a tick is a pure resolver

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10, in the design session that replaced the MVP-01 record with the 4X record. Replaces the ship-kinematics ADR that carried this number; that decision is gone, not superseded (owner decision: the pre-4X record is rewritten in place, `Design/README.md` §4).
**Supersedes:** —

---

## Context

`Design/space-4x-one-pager-v10.md` is the game: a bounded galaxy that is a graph of systems and
lanes, each lane carrying a tick cost fixed at generation; four ticks a day at fixed UTC times;
orders that lock at the tick and resolve in six phases; deterministic integer combat; and a rule
the one-pager states in full — *every phase reads the state at the start of the tick and writes to
the next; nothing reads what another order wrote in the same tick, so processing order can never
change an outcome.* It says in as many words that the game is not real time and not a coordinate
map.

The tree implements a different simulation: one ship in continuous millimetre space, turn-rate
limited, ticked at 20 Hz (`GameLogic/Ship.{h,cpp}`, and the nineteen tests in `GameLogicTests`
that cover it). None of that has a consumer in the 4X. This ADR decides the shape of what replaces
it; `Design/Plans/MVP-02-TheLoop.md` is the plan that builds it and deletes the ship.

Two constraints bind. R16: `GameLogic` uses no `float`, no wall clock, and no iteration over a
container whose order reaches the simulation — and it fits this game better than the one it was
written for, because every quantity here is a count. And `AGENTS.md` §2: `GameLogic` is server-side
and the client never links it, which ADR-013 relies on for the combat preview.

## Options considered

### A. A mutable world stepped in place, phase by phase

`World::Tick()` walks the phases and mutates its own members as it goes. It is the least code and
the natural shape of every real-time simulation, including the one in the tree.

The no-read rule becomes discipline: nothing stops phase 3 reading a fleet strength that phase 2
already changed, and the defect it produces — an outcome that depends on the order fleets happen
to be visited in — is invisible in every test that runs one ordering. It is precisely the defect
class the one-pager's rule exists to exclude.

### B. A pure resolver: immutable input, fresh output

`Resolve(const MatchState& _before, const LockedOrders& _orders, const Rules& _rules) -> TickResult`
where the result holds the state after the tick and one digest per seat. Each phase is a function
from the state so far to a new state; within a phase every write goes to the output and every read
comes from the input, so the no-read rule is a property of the signatures rather than of the
reviewer's attention. The state is copied once per tick.

It costs the copy. Estimated, not measured: at most twelve seats, a galaxy of a few hundred
systems, a few hundred fleets, each record a few dozen integers — tens of kilobytes, four times a
day. It also costs a habit: a phase cannot reach for "the current value" of anything, which is
unfamiliar and is the point.

### C. Event sourcing: orders are events, the state is a fold

Store every locked order and every resolution event; the state at tick N is the fold of all of
them. Replay for free, an audit trail for free, and the digest is a query over the event log.

It is machinery the game does not need at four ticks a day and eighty-odd ticks a match: the
per-tick snapshot ADR-012 stores is already the audit trail, and a fold over a log is a second way
to compute a state that a pure resolver computes directly. If replay from the beginning is ever
wanted, B's snapshots plus the locked orders per tick are exactly the inputs.

## Decision

**B.** `GameLogic` holds a `MatchState` — systems, lanes with tick costs, fleets on a system or on
a lane with departure and arrival ticks, ownership, buildings as unlock state, trade lanes, open
proposals, player states with their counters, scores, the sealed region's opening tick, the tick
number and the end tick — a `Rules` record of every tunable the one-pager names, and a `Resolve`
that is a pure function of the two plus the locked order books. Nothing else in the library holds
state.

**Phases are functions, in the one-pager's order:** lock, production and research, movement,
combat in its two sub-phases, claims and captures, digest. Each takes the state the previous phase
produced and returns a new one. Movement precedes combat so that a fleet ordered out the tick a
hostile arrives escapes; sub-phase 4a, the rear-guard round, is a boolean in `Rules` and is off
until Phase 0 of the test plan says otherwise.

**Every quantity is an integer with its unit in its name** — `strength`, `costTicks`,
`yieldPerTick`, `guardRemainingTicks`. Combat spreads a fleet's damage across enemies in
proportion to strength using integer arithmetic; the remainder of an integer proportional split is
allocated by largest remainder, ties broken by ascending entity id, and never by the order a
container happened to yield. Every entity has a stable integer id assigned by the generator, every
container the resolver walks is ordered by that id, and there is no unordered container in the
library.

**The generator is a function of a seed.** `Generate(seed, seatCount, rules) -> MatchState`,
rejecting any seed that fails the one-pager's guarantees (each capital a rival within three ticks,
one-tick lanes inside a cluster, two-to-four-tick lanes to the frontier) and the layout constraint
of ADR-014. The same seed produces the same galaxy on any machine, which is what makes a Phase 0
bug reproducible from its seed and tick.

**The resolver reads no clock.** The tick number is the clock. When the tick happens is
`NeuronServer`'s business (ADR-007), and the resolver cannot tell a one-hour Phase 0 tick from a
six-hour real one — which is what makes the compressed clock in the test plan a schedule change
and not a rules change.

## Consequences

**What this makes easy.** Every rule in the one-pager is a test with no server, no window and no
thread: build a state, lock some orders, resolve, inspect. The no-read rule has a direct test —
permute the order books and assert the output is byte-identical. The combat preview of ADR-013 is
a call of the same function on a hypothetical state. Persistence (ADR-012) is a serialization of
the input and the output of one call. Determinism is a property of the signatures.

**What this makes hard.** A phase that wants to know what an earlier order in the same phase did
cannot, by construction. That is the rule; when it hurts, the rule is what is being argued with,
and the argument belongs in this file's replacement.

**What it costs.** One copy of the state per tick, and a style that the rest of the tree does not
use.

**What it forecloses.** In-place mutation for speed. At four ticks a day there is nothing to
speed up.

## What this changes elsewhere

- **AGENTS.md:** R16 gains a sentence: the resolver is pure and the schedule lives in
  `NeuronServer`. §2's description of `GameLogic` is reworded.
- **Design/:** ADR-013 (preview), ADR-012 (persistence) and ADR-014 (generator layout) build on
  this. `Design/Plans/MVP-02-TheLoop.md` steps 1 and 2 are this ADR.
- **Code:** nothing yet — this decision is ahead of the code. `GameLogic/Ship.{h,cpp}`, the
  `Ship` half of `GameLogicTests`, and `NeuronCore/Trigonometry.{h,cpp}`'s CORDIC have no consumer
  once MVP-02 step 2 lands, and the plan deletes them. Until then the citations of "ADR-004" in
  `Ship.h` refer to a decision this file no longer records.

## Open questions

The exact `Rules` values — capital guard length, trade lane yield, lane cost ranges, the dominance
threshold and how many ticks it must be held — are Phase 0's to tune and are match data, not
constants. This ADR fixes that they are data; the one-pager gives the starting values.

Whether research exists in the loop MVP at all. The one-pager names it in phase 2 and nowhere
else; the plan treats it as a placeholder phase until the design says what is researched.
