# ADR-017 — The topology is public; what is in a system is fogged until it is scouted

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10, answering the question ADR-005 depends on and the one-pager never states.
**Supersedes:** —

---

## Context

ADR-005 makes the server send each seat a `VisibleSnapshot` filtered by what that seat may see, and
the filter is the one place a leak can hide. It cannot be written until somebody says what
"visible" means, and the one-pager does not.

What the one-pager *does* say constrains the answer from both directions. **Scout** is the first
verb of the core loop, and shared scouting is a proposal that "pays visibly, as fog lifting on the
map" — so something must be hidden. Against that: the galaxy is a graph whose lane costs are
authored so that distance can be reasoned about, "lanes are public", the sealed region is "visible
from tick one" and "everyone can see it and count down to it", the public score always shows the
leader, and the client previews any engagement "from visible information". Those are not the
properties of a dark map. And the one-pager's first interesting decision — near and safe, far and
rich, or toward a rival to deny them — is only a decision if richness is not simply readable off
the map.

ADR-015 adds a practical constraint the design does not: the map is a 3D diorama, and a diorama of
three known systems is not worth drawing.

## Options considered

### A. Full fog

Nothing but the starting cluster exists on the map; the graph itself is discovered node by node.
The most discovery, and the strongest reason to send a scout.

It contradicts the public-lane and visible-region statements, it makes the sealed-region countdown
meaningless until somebody has walked to it, and it gives the diorama almost nothing to draw for
the first days of a match whose whole appeal is opening the app to look. It also makes the first
interesting decision unavailable rather than hard: you cannot weigh far-and-rich against
near-and-safe if you do not know the far system is there.

### B. No fog

Everything except the order books is public. The cheapest filter — the snapshot is the state minus
other seats' books — and it deletes Scout from the core loop, makes shared scouting a proposal
that buys nothing, and turns the first interesting decision into arithmetic.

### C. Topology public, contents fogged

Every seat always sees every system, every lane and every lane cost. What is hidden per system is
its **contents**: its yield, its owner, its buildings and any fleet present. A system is *known*
when the seat has had a fleet at it or at an adjacent system, or when a shared-scouting proposal
gave it; otherwise its contents are unknown. Known-but-not-currently-observed contents are
remembered as of the tick they were last seen, and the map says which tick that was.

## Decision

**C**, with these rules.

**Always visible, to everyone:** the full graph — systems, their map coordinates (ADR-014), lanes
and their tick costs; the sealed region's extent and opening tick; every seat's public state and
score; a capital's guard countdown; and any fleet **in transit on a lane**, with its owner and its
tick-ETA, because the one-pager says a departed fleet is visible in transit and that commitment is
public after the fact.

**Fogged per system, until known:** its yield, its owner, its buildings, and the fleets sitting at
it.

**A system is observed** this tick if the seat has a fleet at it or at a system one lane away. It
is **known** if it has ever been observed by that seat, or if an accepted shared-scouting proposal
covers it while that proposal stands. Observed contents are current; known-but-unobserved contents
are the last observed values, stamped with the tick they were seen, and the client draws them as
stale.

**Fog is per seat and never per player-alliance.** There are no alliances; shared scouting is a
proposal between two seats and lifts fog for the two of them.

**The visibility filter is the only thing that decides this**, and it is a function in `GameLogic`
beside the resolver (ADR-005). No other code, on either side, may decide what a seat sees.

## Consequences

**What this makes easy.** The map is worth drawing from tick one and the countdowns the design
makes public are public. Scouting has a job: it converts a system from a shape into a proposition.
Shared scouting has something concrete to hand over, which is what decision three needs. And
"near and safe, far and rich" is a real gamble, because the far system's yield is a guess until
somebody looks.

**What this makes hard.** Two visibility concepts rather than one — observed and known-but-stale —
and the stale case is the one that will be got wrong. It gets its own tests: a seat that leaves a
system keeps the yield and loses the fleet count; the tick stamp is the tick of last observation
and not the current tick.

**What it costs.** Per-seat known-system sets in the state, which is `seats × systems` bits and
nothing. And an adjacency query per fleet per snapshot, which at a few hundred fleets four times a
day is nothing.

**What it forecloses.** A hidden map, and with it the kind of exploration a single-player 4X opens
with. This game's discovery is about what rivals did, not about where the systems are.

## What this changes elsewhere

- **AGENTS.md:** no rule changes.
- **Design/:** ADR-005 (this is the filter's rule), ADR-015 (the scene draws known, stale and
  unknown differently). `Design/Plans/MVP-02-TheLoop.md` slices 1 and 2.
- **Code:** nothing yet.

## Open questions

Whether a scout should reveal a system's contents at one lane's remove, as decided here, or only on
arrival. One lane makes a scout worth sending along a chain and makes the frontier legible a tick
sooner; on arrival makes it slower and more deliberate. Decided as one lane, and it is a `Rules`
value so Phase 0 can try zero.

Whether shared scouting shares the sharer's whole known set or only what they currently observe.
The plan shares the observed set, since sharing a memory of tick three at tick forty is a tell
nobody can read.
