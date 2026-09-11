# ADR-025 — The seam speaks in bytes

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Build session for `Design/Plans/4X-02-ServerAndClient.md`, Step 1.
**Supersedes:** — (succeeds ADR-007, which is Deprecated)

---

## Context

ADR-007 put a `Simulation` interface in `NeuronCore` so that `NeuronServer` could drive a game
without depending on one. Its interface was `ApplyOrder(MoveToOrder) / Tick() / Snapshot()` and
ADR-015 deleted everything it named. The **argument** survived and is the reason this ADR exists at
all: `AGENTS.md` §2 forbids the edge `NeuronServer → GameLogic`, so the server cannot name a single
game type, and the interface between them has to live in `NeuronCore`, which cannot either.

What changed is the shape of what crosses. ADR-007's game was one ship and one order; this one is
an order *set* per player per tick, a per-player snapshot, and a per-player digest — because players
see different things (ADR-022) and decide simultaneously (ADR-019).

There is a second constraint ADR-007 did not have. ADR-024 makes a match store the locked order
sets, and the session is what writes them. So the server does not merely *route* orders, it
**keeps** them — which sharpens the question of what it is allowed to know about them.

## Options considered

### A. Templates: `Session<TSimulation>`

No virtual calls, no interface, and the dependency inverts at compile time. It also means
`NeuronServer` is header-only, the session's implementation lives in every translation unit that
instantiates it, and — fatally — `NeuronServerTests` can only test the session by instantiating it
over a real game, which is the coupling this is trying to avoid. ADR-007 rejected this and was
right.

### B. An abstract class over game types

`virtual void Submit(PlayerId, const OrderSet&)`. The obvious shape, and impossible: `NeuronCore`
cannot name `Lockstep::OrderSet`. Moving those types into `NeuronCore` to make it possible would
put the game's vocabulary in the engine and break R9 far more seriously than the alternative.

### C. An abstract class over bytes

`virtual void Submit(std::int32_t player, std::span<const std::uint8_t> orders)`. Orders arrive
opaque, snapshots and digests leave opaque, and everything the server legitimately needs — the tick,
the player count, whether the match is over, the hash — is an explicit method.

The cost is that the server cannot inspect, validate or log the *contents* of anything it carries.

### D. A message bus with typed payloads and registered handlers

The generalised version of C, with a registry so new message kinds arrive without changing the
interface. Rejected as machinery for a problem nobody has: there are three things crossing this
seam and the plan that follows adds none.

## Decision

**C. `Neuron::Simulation` is an abstract class over bytes.**

```
PlayerCount / Tick / IsFinished / Hash / Configuration
Submit(player, bytes) / MarkPresent(player) / Resolve()
LockedTurn() / SnapshotFor(player) / DigestFor(player)
```

`Lockstep::MatchSimulation` implements it and lives in **`GameLogic`**, not `NeuronServer`. The game
reaches *up* to the interface; the server never reaches down. `Lockstep.exe` is the only
thing that sees both, which is exactly what a composition root is for.

**The opacity is a feature, not a tolerated cost.** A server that could read an order could be
tempted to act on one, and the day it does, the simulation has stopped being authoritative. Because
`NeuronServerTests` can only see bytes, its tests fail for server reasons — a lock missed, a store
truncated, a reload that did not reproduce — and never because somebody changed a combat rule.

**Decoding happens at the game's edge, immediately.** `MatchSimulation::Submit` decodes the order
set there and then, rejects it if it is malformed, if it has bytes left over, or if it names a
different player than the one who sent it, and counts the rejections. That is not defensiveness for
its own sake: `ByteReader` fills a truncated record with **zeros**, so a corrupt submission that was
decoded lazily at the lock would arrive at `Match::Validate` looking like a perfectly well-formed
order from player 0.

**`PlayerTurn` carries presence beside the orders**, because they are different facts and the
custodian rule reads both. A store that kept only the orders would replay into a different world
than the one that was played.

**`Configuration()` stores the *accepted* seed**, not the one the generator was first offered. The
generator is a search that can reject seeds; storing the offered one would regenerate a different
galaxy if the rules ever changed which seeds are acceptable.

## Consequences

**The server's tests need a fake, and that is the point.** `CountingSimulation` is forty lines whose
entire state is a hash of the turns it was given in order — enough to prove that a reload replays
the same turns in the same order, without this test file knowing what a fleet is.

**The server cannot help diagnose a bad order.** It can report *that* a submission was refused
(`RejectedSubmissions`) and nothing about why. In Phase 0, with six known people, that is enough;
if it stops being enough, the answer is a diagnostic channel on this interface, not a peek through
it.

**Every crossing costs an encode and a decode.** At four messages a day per player this is not worth
a sentence of optimisation, and the measurement in `Design/Reference/tick-resolution-cost.md`
excludes it precisely because it is noise at this scale.

**`MatchRules` has to be serialised field by field**, and `MatchSimulation.cpp` checks the field
count on read. A field added to the struct and not to the writer would silently revert to its
default on reload — and the hash check would catch it, but report a *determinism* failure, which is
the wrong diagnosis and the expensive one to chase.

## What this changes elsewhere

`NeuronCore` gains `Simulation.h`. `GameLogic` gains `MatchSimulation`. `NeuronServer` gains
`Session` and `MatchStore` and loses its `SuiteSmoke`. ADR-007 stays Deprecated and is worth reading
for the reasoning, which this ADR reuses rather than restates.

## Open questions

**Whether `Configuration()` should carry a version.** The field count catches a rules struct that
changed shape, which is the likely failure. A build whose rules have the same *count* and different
*meaning* would pass that check and fail the hash check instead — correct, but with a confusing
diagnosis.

**Whether the session should see the digest at all.** It routes digests as bytes today. The test
plan's instrumentation wants proposal and lane events logged, and those are *in* the digest — so
either the session learns to read one, or the game emits an instrumentation stream beside it. The
second is cleaner and is the one to take.
