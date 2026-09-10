# Galaxy generation — the constraint numbers, and where each one is enforced

**What this is.** A Reference (`Design/README.md` §2): the numbers the bounded galaxy is built to,
and the place in the tree that each is checked. It records what is *true* on 2026-09-10, after
steps 0–2 of [`../Plans/4X-01-CoreLoop.md`](../Plans/4X-01-CoreLoop.md). Nothing here is a decision.
The constraints themselves come from `../space-4x-one-pager-v10.md`; the determinism they rest on is
[ADR-018](../ADR/ADR-018-the-simulation-is-a-pure-function.md).

**Measured** on 2026-09-10 by `Tests/GameLogicTests/GameLogicTests.cpp`, over every player count
from 6 to 12 and the eight fixed seeds that file names, on `x64\Debug` — 25 tests, all passing. The
sizes in §3 are read off generated galaxies rather than derived, and the test that reads them fails
if they change.

**Not implemented, and not claimed:** nothing in the galaxy is *played* yet. There are no fleets, no
production, no orders and no resolver — those are steps 3 onward of the plan. The sealed region is
placed and drawn; nothing happens when it opens (step 9). Ownership exists only as the capital each
player starts on.

---

## 1. The five constraints

The one-pager states them in prose, scattered across two sections. Collected, they are the whole
specification a generated galaxy has to satisfy:

| # | The constraint | Where it is checked |
|---|---|---|
| 1 | The galaxy is bounded and connected — every system reachable from every other | `Galaxy::IsConnected`, via `GalaxyGenerator::Validate` |
| 2 | Each capital has a rival capital within **three ticks** by shortest path | `GalaxyGenerator::Validate`, one Dijkstra sweep per capital |
| 3 | Lanes inside a starting cluster cost **one tick** | `GalaxyGenerator::Validate`, per lane |
| 4 | Lanes leaving a cluster toward the frontier cost **two to four ticks** | `GalaxyGenerator::Validate`, per lane |
| 5 | The sealed region exists, is unowned, and something reaches it | `GalaxyGenerator::Validate`, on `Galaxy::RegionAnchor` |

Two things about that table are deliberate and worth stating, because both are easy to undo by
accident.

**Validation reads only the finished graph.** `Validate` takes a `const Galaxy&` and a `MatchRules`,
and it recomputes distances from the lanes rather than consulting anything the generator worked out
on the way. A generator that validated using its own notes would agree with itself about a mistake:
if the code that decided a lane costs one tick is also the code that reports it costs one tick, the
check is a tautology. This is why constraint 2 costs twelve Dijkstra sweeps over sixty nodes rather
than being asserted at the point the lane is added.

**A seed that fails is rejected, not repaired.** `TryGenerate` returns a `GalaxyRejection` naming
the constraint that was broken. There is no path in the generator that nudges a lane cost or adds an
edge to make a failing galaxy pass. `Generate` retries with a *different seed* — up to
`maximumSeedAttempts` — and if every attempt is refused it calls `Neuron::Fatal`, because the layout
in §2 satisfies these constraints by construction and a full run of refusals therefore means the
rules and the generator have drifted apart. That is a defect, not bad luck.

## 2. The layout, and the arithmetic that makes three ticks work

Every galaxy is a ring. The region anchor is placed first, at the centre of the design space, so its
id is always 0. Capitals are spaced evenly around an outer ellipse, one per player. Each capital
gets a starting cluster of satellites fanned *outward*, away from the middle, joined to it and to
each other by one-tick lanes. A border system sits at each midpoint angle between two neighbouring
capitals. Inside the ring is the frontier, and the region sits at its centre.

The ring is the fairness argument. Every capital has exactly two neighbours at the same distance and
the region is equidistant from all of them, which is what the test plan's Phase 2 asks for when it
says no empire should be consistently positioned to dominate the region at opening. A layout in
which somebody starts nearest fails that before anyone plays.

The three-tick guarantee falls out of one asymmetry: **a border belongs to the earlier of the two
clusters it sits between.** So the lane from that capital to its border is *inside* a cluster and
costs one, and the lane from the border on to the next capital *leaves* a cluster and costs the
frontier minimum, two. One plus two is three, for every adjacent pair, on every seed — constraint 2
is satisfied by construction rather than by search. That is why `rejectedSeeds` is zero for every
player count, and why a non-zero value is worth investigating rather than shrugging at.

Positions are integers in an 800×560 design space and exist **only to be drawn**. Nothing in the
simulation reads them: distance is authored as a lane cost, never measured from coordinates. The
rings are ellipses rather than circles because the design space is wider than it is tall.

## 3. The numbers

Defaults live in `GameLogic/MatchRules.h` and are data, not constants, because the test plan's Phase
0 exists to tune them. Repeating a value here would be a second copy to go stale, so this section
records the ones the *generator* reads and what each one does to the shape.

| Field | Default | Effect on the galaxy |
|---|---|---|
| `playerCount` | 6 | Capitals, and therefore clusters, borders and frontier systems |
| `satellitesPerCapital` | 2 | Systems in a starting cluster besides the capital |
| `frontierSystemsPerPlayer` | 1 | Systems in the contested middle, floored at three |
| `maximumTicksToNearestRival` | 3 | Constraint 2 |
| `frontierLaneMinimumTicks` | 2 | Constraint 4, and the cost of a border→capital lane |
| `frontierLaneMaximumTicks` | 4 | Constraint 4 |
| `maximumSeedAttempts` | 64 | How many refusals before `Generate` calls it a defect |

Player count is bounded at **6 to 12** (`MINIMUM_PLAYERS`, `MAXIMUM_PLAYERS`). Outside that range
`TryGenerate` refuses before building anything, rather than producing a galaxy nobody designed for.

Sizes, measured on 2026-09-10 and identical across all eight test seeds:

| Players | Systems | Lanes |
|---|---|---|
| 6 | 31 | 45 |
| 7 | 36 | 53 |
| 8 | 41 | 60 |
| 9 | 46 | 68 |
| 10 | 51 | 75 |
| 11 | 56 | 83 |
| 12 | 61 | 90 |

Systems are `1 + 5·players` — the region, plus a capital, two satellites, a border and a frontier
system for each player. Lanes are `7·players + ⌈players/2⌉`: seven per player (three inside the
cluster, two through the border, two out from the frontier system), plus the region's approaches.
The region is joined to every *second* frontier system, which is why the step between adjacent rows
alternates between eight and seven rather than being constant — an odd player count rounds up and
gains the extra approach.

Systems are named from a table of **64** embedded names, drawn without replacement by a Fisher-Yates
shuffle. Sixty-four is the smallest round number above the sixty a twelve-player galaxy needs; a
thirteenth player would need more names as well as a new design.

## 4. What makes it reproducible

The galaxy is a pure function of `(MatchRules, seed)`. Same inputs, same graph, on every machine —
which is what makes a match the same match for everyone playing it. ADR-018 gives the four
mechanisms in full; three of them are visible in this code and are the ones to preserve when editing
it.

The **PRNG is splitmix64, pinned by value** in `Tests/NeuronCoreTests/NeuronCoreTests.cpp` against
the published vectors. Bounded draws use rejection rather than modulo, so a lane cost drawn from the
two-to-four band is not quietly biased toward two.

**Trigonometry is integer.** The ring is laid out through `NeuronCore/Turns16.h`, which computes
sine by Bhaskara I's approximation in `std::int64_t` — worst error measured at 2/1024, well under
half a unit at the radii used here. `std::cos` is not required to be correctly rounded and
implementations disagree in the last bits, so a galaxy laid out with it would be laid out
*differently* on a different standard library.

**Every iteration that reaches a result has a defined order.** The Dijkstra in `Galaxy.cpp` orders
its frontier by `(cost, systemId)` rather than by cost alone. The tiebreak is not decoration: two
systems at equal cost must be visited in the same order everywhere, or the accept/reject decision in
§1 could differ between two machines running the same seed.

## 5. What is not pinned, and why

`GameLogicTests` asserts that the same seed produces the same galaxy by comparing a hash **between
two runs**, not against a literal. The distinction is the point. The PRNG sequence underneath has to
stay fixed forever and is pinned by value; the *layout* on top of it is still Phase 0's to tune, and
a literal hash here would fail on every deliberate change and teach whoever is tuning to update it
without reading it.

When the layout is frozen — after Phase 0, not before — pinning a hash per player count becomes
worth doing, and this section is the note saying so.
