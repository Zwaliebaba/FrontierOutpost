# ADR-014 — The galaxy generator emits display coordinates, and a lane's drawn length is monotone in its tick cost

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10.
**Supersedes:** —

---

## Context

The one-pager is explicit: the galaxy is a graph, every lane carries a tick cost assigned at
generation, and *distance is authored, not emergent*. ADR-015 draws that graph as a 3D diorama on a
ground plane. A picture of a graph on a plane has lengths whether anyone intended them or not, and
a player reads a long lane as a slow one. If the drawn length and the tick cost disagree, the map
lies about the thing the design says matters most — tempo — and the lie is in the primary
spatial view.

The generator already rejects seeds that fail the one-pager's guarantees (ADR-004). Layout is one
more constraint of the same kind.

## Options considered

### A. The client lays the graph out, and lanes carry tick-cost markers

The generator emits only the graph. The client runs a layout — force-directed, or a spring
relaxation — and draws pips on each lane for its cost. Less generator work. The layout is not
deterministic across clients unless it is made so, two players' maps differ, the digest cannot
say "north", and the pips are what the player reads while the lengths are what the player sees.

### B. The generator emits coordinates under a monotonicity constraint

Each system gets a position on the map plane, in whole map units, assigned at generation. The
constraint: for any two lanes, the one with the higher tick cost is drawn at least as long; lanes
of equal cost may differ in length within a bounded ratio. Seeds whose graph cannot be placed
under it are rejected like any other. Every client draws the same map; the map is part of the
seed's identity; and the picture never contradicts the cost.

It costs a placement algorithm in the generator and a rejection rate that has to be measured, and
it forbids two lanes from crossing only if the generator also enforces that — which it does, since
a crossing lane is a lane the eye reads as a junction.

## Decision

**B.** `Generate` (ADR-004) assigns every system integer coordinates on the ground plane and
rejects a seed unless every lane's drawn length is monotone non-decreasing in its tick cost across
the whole galaxy, no two lanes cross, and no two systems are closer than a minimum separation
fixed in `Rules`. The coordinates are part of `MatchState`, cross the wire in the
`VisibleSnapshot` (ADR-005), and are the positions ADR-015's scene draws at. The one-pager's
dense-start, sparse-frontier pacing is then visible: clusters are tight because their lanes are
one tick, and the frontier is far because its lanes are four.

**The length-to-cost relation is a `Rules` value**, so Phase 0 can widen the map without touching
the tick, as the one-pager asks.

## Consequences

**What this makes easy.** The scene draws what it is given. Every player sees the same galaxy.
"The system to the north of your capital" is a sentence the digest can use.

**What this makes hard.** The generator is harder, and its rejection rate at eight and twelve seats
is a figure that has to be measured before the size-per-player is tuned. A generator that rejects
most seeds is a slow match creation, which is tolerable; one that rejects all of them at twelve
seats is a design problem, which is why the figure is measured at MVP-02 step 1.

**What it costs.** A placement algorithm that does not exist, and a test that walks every pair of
lanes.

**What it forecloses.** Layout as a client concern, and with it a map that reflows to a screen.
The map is authored.

## What this changes elsewhere

- **Design/:** ADR-004 (the generator), ADR-003 (map bounds and units), ADR-015.
  `Design/Plans/MVP-02-TheLoop.md` step 1.
- **Code:** nothing yet.

## Open questions

Whether the sealed region needs a distinct placement rule — central, so every capital is roughly
equidistant, as "a race, not a reward" implies. The plan places it at the graph's centre by tick
distance and measures the spread; a rule follows if the spread is unfair.
