# ADR-005 — After every tick the server sends each seat its visible snapshot and its digest; the client never simulates

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10. Replaces the replication-and-interpolation ADR that carried this number (owner decision: the pre-4X record is rewritten in place, `Design/README.md` §4).
**Supersedes:** —

---

## Context

The tree replicates a 32-byte ship state twenty times a second to a client that interpolates
between the last two (`FrontierOutpost/ShipView.{h,cpp}`). In the 4X nothing happens between
ticks, so there is nothing to interpolate; and what a seat is allowed to see differs from what the
next seat sees — orders are hidden until they lock, and shared scouting lifts fog for the two
seats that agreed to it and nobody else. A broadcast of the whole state is therefore not merely
wasteful; it leaks the one thing the design says must be hidden.

The one-pager also fixes what the client's primary screen is: the digest, one per tick, sorted by
consequence, never one notification per event. The map is second, with commitments drawn as
overlays. So the two things a seat needs after a tick are already named.

## Options considered

### A. Broadcast the full state after each tick

One record for everyone, as the tree does now. Simplest, and wrong: every seat receives every
other seat's order book, and fog does not exist.

### B. A per-seat visible snapshot and a per-seat digest, pushed after each tick and readable on demand

The server filters the resolved state through the seat's visibility and sends that, with the
digest the resolver produced for that seat. A client that reconnects asks for the same two things
for the current tick. The client holds them and draws them.

It costs a visibility filter on the server, which is game logic and lives in `GameLogic` beside
the resolver, and a per-seat send, which at twelve seats and four ticks a day is nothing.

### C. Events only; the client reconstructs the state

Send the digest and let the client apply it to its last snapshot. Smaller messages, and a client
that has to hold a rule for every event type — which is the one thing `Design/README.md` §1 says
the client does not do — and that diverges silently the first time it misses one.

## Decision

**B.** After the resolver runs, the server computes for every seat a `VisibleSnapshot` — the
systems, lanes and fleets that seat can see, each fleet in transit with its departure and arrival
ticks, ownership, buildings, trade lanes, open proposals with their remaining ticks, every seat's
public state and score, the countdowns the one-pager makes visible (capital guard, sealed region,
match end), the seat's own order book for the next tick, and the tick number — and a `Digest`,
the resolver's list of events for that seat sorted by consequence, with the round-by-round
strengths of every engagement the seat could see. Both are sent over the transport of ADR-006 and
both are answerable to a request at any time.

**The client holds the latest snapshot and digest and draws them.** It does not advance either,
does not predict, and cannot move a fleet because something was tapped: the only things that
change what it shows are a snapshot arriving and its own order book being acknowledged (ADR-006).
The motion it draws between ticks is a derivation from the snapshot and the clock and is the
subject of ADR-015; none of it goes back to the server.

**Before the first snapshot the client draws nothing and says so**, exactly as the tree does now
for the first ship state. With a network between them the wait is no longer one tick; it is a
connection, and the status line says which.

**The snapshot is complete, never a delta.** A missed message is corrected by the next request, and
a client that has been away for a week needs no history to be right.

## Consequences

**What this makes easy.** The client has no rules and cannot leak what it was not sent. Fog and
hidden orders are enforced where the state lives. Reconnect and "open the app after work" are the
same code path as the push.

**What this makes hard.** The visibility filter is a second function over the state that has to
agree with the resolver about what "visible" means, and it is the one place a leak can hide. It
gets its own tests: a seat must never receive another seat's order book, and a fleet in a system
the seat cannot see must not appear.

**What it costs.** A full snapshot per seat per tick. Estimated, not measured: a few hundred
records of a few dozen integers, tens of kilobytes, four times a day per seat.

**What it forecloses.** Client-side reconstruction from events, which C offers and this design has
no use for.

## What this changes elsewhere

- **AGENTS.md:** no rule changes; §2's statement that the client never links `GameLogic` is what
  this relies on.
- **Design/:** ADR-006 carries the messages, ADR-015 the client's use of them.
  `Design/Plans/MVP-02-TheLoop.md` step 3 defines the records, step 6 the client.
- **Code:** nothing yet. `FrontierOutpost/ShipView.{h,cpp}` and `Neuron::ShipState` have no
  consumer once MVP-02 step 6 lands and the plan deletes them; until then `ShipView.h`'s citation
  of "ADR-005" refers to a decision this file no longer records.

## Open questions

How large a snapshot is at twelve seats on a real generated galaxy. Measured at MVP-02 step 3 and
recorded in the plan's close-out; if it is large enough to matter on a phone, a delta path is a
later ADR, not a change to this one.
