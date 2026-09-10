# MVP-02 — The loop: a match of eight seats, resolved four times a day, on a map worth looking at

**Status:** Plan, not started. Owner decisions recorded 2026-09-10; §2 is the complete list and ADR-003 to ADR-016 are the record. Nothing in this plan is built.
**Read first:** [`AGENTS.md`](../../AGENTS.md) in full, then [`Design/README.md`](../README.md) §1, then [`space-4x-one-pager-v10.md`](../space-4x-one-pager-v10.md) and [`space-4x-prototype-test-plan.md`](../space-4x-prototype-test-plan.md), then every ADR from 003 up. This plan does not repeat any of them; it depends on all of them.

This is the prompt for the build sessions that turn the MVP-01 tree into the loop the test plan's Phase 0 runs on. When the work is done, move this file to `Design/Archive/` with a "what shipped, what did not" section at the top, as MVP-01 did.

---

## 1. What "done" looks like

`FrontierServer.exe` creates a match from a seed for eight seats on a compressed one-hour schedule,
prints eight tokens, and runs unattended. Eight `FrontierOutpost.exe` clients on eight machines
present a token each, see the same generated galaxy as a 640×400 sixteen-colour isometric diorama,
place fleet, build and proposal orders that lock together at the hour, and after every lock read a
digest of what changed and a map on which their fleets have visibly moved. A capital cannot be
attacked for twelve ticks and says so. A seat that does not log in for three ticks is a custodian
on everyone's map. A trade lane is proposed from the build menu, accepted from the digest, and pays
from the tick it was accepted at. The match ends on the tick it said it would, with placements.
The server was restarted in the middle of it and nobody noticed.

On one machine with no network, `FrontierOutpost.exe` with no arguments does all of the above for
all eight seats in one process, with a seat selector and a *Resolve now* button.

That is the one-pager's build order — *loop first: graph generator, tick resolution, trade lanes
and proposals in the build menu, custodian, capital guard, siege rule, digest, fixed end* — and
nothing from Phase 2. No Exile, no sealed region, no colony core, no upkeep or salvage. The sealed
region is *generated and visible* (the one-pager makes it visible from tick one) but nothing can
enter it; that is Phase 2's.

## 2. Decisions already made (do not reopen these)

| | Decision | Where it binds |
|---|---|---|
| **The game** | The one-pager, v10, is the game. The tree's real-time ship was a proof of the stack, not the game. | Owner, 2026-09-10 |
| **Platform** | The Windows D3D12 640×400 client stays the client. Mobile is a later port and a later ADR. | Owner, 2026-09-10; `Design/README.md` §1 |
| **Process shape** | `FrontierServer.exe` headless; `FrontierOutpost.exe` the client and, with no arguments, the harness hosting every seat in-process. | ADR-011 |
| **Persistence** | One snapshot file per tick plus the pending order books, in a match directory beside the server. The client still ships alone. | ADR-012 |
| **Combat preview and replay** | Computed by the server on request from visible information; the digest carries the round log. The client holds no rules. | ADR-013 |
| **World and tick** | A graph with authored lane costs; a pure resolver in six phases; integer arithmetic with a stated remainder rule; sub-phase 4a a `Rules` flag, off. | ADR-004 |
| **Schedule** | Four ticks a day at fixed UTC times as match data; one hour for Phase 0, six for Phase 1. The resolver reads no clock. | ADR-007 |
| **Replication** | A per-seat visible snapshot and a per-seat digest after every tick, complete, never a delta. | ADR-005 |
| **Transport** | Reliable, ordered, framed, versioned messages over TCP; every request answered; loopback carries the same frames. | ADR-006 |
| **Map layout** | The generator emits display coordinates; drawn lane length is monotone in tick cost; seeds that cannot be placed are rejected. | ADR-014 |
| **The scene** | A 3D diorama of authoritative state; cosmetic time between ticks; orders are two ids, never a coordinate. Digest first, map second. | ADR-015 |
| **Camera** | 2:1 dimetric in whole pixels, snapped, panning over bounded map coordinates; zoom about the point under the pointer. | ADR-003, ADR-008 |
| **Identity** | A token per seat; presenting it is logging in. No accounts before Phase 1 gates. | ADR-016 |
| **Documents** | The one-pager's text is current and the test plan's "message" was stale: v1 has no free text. Both documents were corrected on 2026-09-10. | Owner, 2026-09-10 |
| **Timeline scrubber** | Not in this MVP. Watch item after Phase 1. | Owner, 2026-09-10; ADR-015 |
| **Presentation, palette, shading, starfield, input** | Unchanged from MVP-01. | ADR-001, ADR-002, ADR-009, ADR-010 |

## 3. What the tree loses

Say it before building, so nobody preserves it by reflex.

- `GameLogic/Ship.{h,cpp}` and every `ShipKinematicsTests` method. `WorldTests` is rewritten
  against the new `World`.
- `Neuron::MoveToOrder`, `Neuron::ShipState`, `MOVE_TO_ORDER_BYTES`, `SHIP_STATE_BYTES` and their
  four serialize functions and tests. The `Write`/`Read` cursors stay; they are the format.
- `FrontierOutpost/ShipView.{h,cpp}`. There is nothing to interpolate.
- `NeuronCore/Trigonometry.{h,cpp}` — CORDIC, `Atan2Turns16`, `SineCosineTurns16`,
  `IntegerSquareRoot`, `SaturatingAdd` — and `TrigonometryTests`. No consumer remains after step 2.
  `Turns16` goes with it. If step 6 wants an integer angle for a mesh heading it is a `float` on
  the client, where R16 does not reach.
- `Session::TICKS_PER_SECOND`, `TICK_MICROSECONDS`, and `TheTickRateIsTwentyHertz`.
- `LoopbackTransport`'s two overflow policies as a statement about the game (ADR-006).
- The status line's `X` and `Z`. The tick stays.

Until each step lands, the code comments that cite ADR-004, ADR-005 and ADR-006 describe decisions
those files no longer record. That is known and is fixed by deletion, not by editing comments.

## 4. The work, in order

Each step ends green: builds Debug|x64, every suite passes, the three checkers pass, and where a
step says **run**, the executable was run. Do not start the next step on a red one. Commit at each
step boundary; update `Design/` in the same commit as the code it describes.

### Step 0 — Read, then plan out loud

Read everything §0 of this file names, then `GameLogic/`, `NeuronCore/Simulation.h`,
`NeuronCore/Protocol.{h,cpp}`, `NeuronServer/Session.{h,cpp}` and `FrontierOutpost/FrontierOutpost.cpp`.
Report what is there, which of §3 you will delete at which step, and where the one-pager is
silent on something the resolver has to decide (there are several — production values, fleet
strength units, what a building is). If anything in this plan contradicts an ADR or the tree, say
so and stop.

### Step 1 — `GameLogic`: the state and the generator

`MatchState`, `Rules`, the id types, and `Generate(seed, seatCount, rules)` per ADR-004 and
ADR-014. Every entity has a stable integer id; every container is a `std::vector` sorted by id or a
`std::map`; there is no unordered container in the library and `CheckProjectFiles.py` is extended
to fail one in `GameLogic/`. Every quantity is an integer with its unit in its name.

Tests in `GameLogicTests`, and this is the suite that matters most: the generator's guarantees
(each capital a rival within three ticks by shortest path, one-tick lanes inside a cluster, two-
to four-tick lanes to the frontier), rejection of a seed that fails them, the layout constraint
(walk every pair of lanes: higher cost never shorter, none cross, minimum separation), the sealed
region generated and marked, and byte-identical output for the same seed across two runs and — by
serializing — the same seed on any machine. **Measure and record the rejection rate at 6, 8 and
12 seats over a thousand seeds**; ADR-014 needs the figure, and if it is near one at twelve, stop
and report before tuning.

Delete `Ship.{h,cpp}` and its tests here. `World` becomes a thin owner of a `MatchState`.

### Step 2 — `GameLogic`: the resolver

`Resolve(before, lockedOrders, rules) -> TickResult` and its six phases as functions, per ADR-004.
Orders: `MoveFleet(fleetId, systemId)`, `Build(systemId, buildingKind)`, `Propose(kind, seat, …)`,
`Accept`, `Decline`, `Withdraw`, `Concede`. Proposals per the one-pager's paragraph in full: lock
with the others, four ticks open, withdrawable, re-validated at every lock and voided with a
reason, conditional order, accepted-at-lock pays from that tick, *ignored* reported to the
proposer. Trade lanes: a building with two owners; cancelled by either at any lock; auto-cancelled
on losing an endpoint, and the digest says which. Player states: the four states and six
transitions and nothing else; custodian on three ticks of absence, reversible; on concession,
permanent; garrisons weaken per tick of absence; conquered-from-custodian yields half for the rest
of the match; a first-week custodian scores nothing. Capital guard as a countdown in `Rules`.
Siege: two consecutive ticks of uncontested hostile presence. Score, the fixed end tick, and a
dominance threshold held for N ticks. The visibility filter that produces a `VisibleSnapshot` and
the `Digest` sorted by consequence, with an `EngagementLog` per engagement. `Preview` as the same
combat functions over a visible snapshot (ADR-013).

Tests, one class per phase and one per rule the one-pager states, and three that are the
architecture: **permute every order book and assert the resolved state is byte-identical**;
**movement before combat lets a fleet ordered out escape**, and with the 4a flag on, it takes one
round from the arrivals' end-of-movement strength; **the remainder of a proportional split lands
by largest remainder, ties by ascending id**. Combat: incumbent bonus, none for simultaneous
arrivals at an empty system, a tie is mutual attrition. Claims: two surviving hostiles leave a
system unclaimed; an arriving fleet that dies contests nothing. Every countdown the client will
draw is a number in the snapshot.

Research is a phase that does nothing, named as such, until the design says what is researched.

### Step 3 — `NeuronCore`: the protocol and the seam

The frame, the message types, the records, the `Verdict` and the reason codes per ADR-006; the
widened `Simulation` per ADR-007. Round-trip tests for every record, the little-endian test kept,
a test that a frame with an unknown version is refused, and a test that a `VisibleSnapshot` for
seat A serialized and deserialized contains no order of seat B. Delete `MoveToOrder` and
`ShipState`. Name the `Transport` concept; `LoopbackTransport` carries frames.

**Measure a snapshot's size** on an eight-seat generated galaxy and record it in ADR-005's open
question.

### Step 4 — `NeuronServer`: seats, books, schedule, persistence, socket

`Session` per ADR-007: seats and tokens (ADR-016), an order book per seat with `Submit` and
`Withdraw` answered, `TickSchedule` in UTC against `system_clock`, `ResolveNow()`, the per-seat
send after every tick, `Save`/`Load` and the match directory per ADR-012 with `Pending.bin` written
on every accepted edit, and the event log the test plan names (login, session start and end,
order edit, order lock, proposal sent, accepted and declined, trade lane opened and cancelled,
capital fall, custodian takeover, fleet order after capital fall) as one append-only file. A
`TcpTransport` against Winsock in `NeuronCore`, tested with two ends in one process.

Tests in `NeuronServerTests` with a counting simulation, as now: the schedule fires at the right
UTC times (drive it with an injected clock — the only place a clock is injected, and it is the
server's, not the simulation's); orders after the lock go to the next book; a seat receives its
own snapshot and not another's; restart from a match directory resumes at the right tick with the
pending books intact; a `Join` with a bad token is refused.

### Step 5 — `FrontierServer.exe`

A new project, `FrontierServer/`, `namespace Frontier`, referencing `NeuronCore`, `NeuronServer`
and `GameLogic`. Command line: create (seed, seats, schedule, rules file or defaults) and run
(match directory). Prints the tokens. No window. Register it in `FrontierOutpost.slnx`, extend
`Build/CheckProjectFiles.py` and `.github/workflows/build.yml`, and update `AGENTS.md` §2's map and
graph in the same commit. **Run it**: create an eight-seat match on a one-minute schedule, watch
three ticks resolve and three snapshot files appear, kill it, restart it, and see it resume at
tick four.

### Step 6 — `FrontierOutpost.exe`: the harness and the client

Two halves, and the harness first because it is what every later session tests with.

**6a — the harness.** With no arguments: a `Session` in-process over the loopback, every seat's
token held, a seat selector drawn with the 8×8 font, a *Resolve now* target, and the same client
code as 6b talking to it. With a server address and a token: connect over `TcpTransport`.

**6b — the client.** `Frontier::Scene` per ADR-015, derived from the four inputs and nothing else.
The **digest** as the opening screen, a list, each entry a tap target that calls `LookAt()`. The
**map**: systems as meshes in the family of `StationMesh.h`, lanes as thin quads, fleets as
`ShipMesh.h`, at the generator's coordinates; fleets in transit at their cosmetic-time position
with an ETA label; own pending orders as unlit ghosts; the sealed region marked with its opening
countdown; custodians flagged "since tick N"; capitals with their guard countdown. The **orders
screen**, three columns — fleets, builds, proposals — reading from the acknowledged book. The
**build menu** on a selected system, with *Trade lane with [neighbour]: +X/tick* greyed and
*Propose* where *Build* would be. The **income screen** with lane income foregone. First contact's
prompt: *Contact: [player]. Propose trade lane?* Pending proposals in the digest as countdowns.

Engine additions in `NeuronClient`, none of which knows what a fleet is: the panning camera with
bounds and `LookAt()` (ADR-003), zoom about an anchor (ADR-008), picking against projected bounds,
drag and long-press in `PointerInput`, a thin-quad line path, and an animation clock. Tests for
each in `NeuronClientTests`, the picking ones exact.

**Run it**, with a mouse or a finger, and say which. Take a seat, place a fleet order by selecting
a fleet and tapping a system, resolve, and watch the fleet on the lane. Propose a lane from one
seat, accept from another, resolve, and read both digests. **Measure legibility**: at the zoom
where one empire fits, how many systems are on screen and whether the fleets can be tapped
(ADR-015's open question). Record the numbers.

The system view and the combat replay are **not** in this step. They are the first work after the
Phase 0 gate, and they are cheap then because the `EngagementLog` and the meshes exist.

### Step 7 — Phase 0 dry run, and close out

Eight clients against `FrontierServer.exe` on a one-hour schedule — the harness does not count —
for at least one full day, by whoever can be found. Read the event log and confirm every event the
test plan names was recorded. Note every place the one-pager was silent and what the resolver
decided, as candidate ADRs.

Then: this plan to `Design/Archive/` with its "what shipped" section; ADR open questions answered
where a figure now exists; `AGENTS.md` §2 true to the tree; `SuiteSmoke` deleted from any suite
that has a real test.

## 5. Things that will tempt you, and the answer

- **"I'll let the client move the ghost as soon as the order is accepted, before the tick."** No.
  The ghost is drawn from the acknowledged book and stays a ghost until a snapshot says otherwise.
  ADR-015's derivation rule is the whole architecture.
- **"The resolver would be simpler if phase 3 could just update the fleet in place."** It would,
  and then the no-read rule is a hope. Immutable in, fresh out (ADR-004).
- **"A `float` for the yield fraction."** R16. Integers with a remainder rule.
- **"An `unordered_map` keyed by id, it's faster."** R16, and there is nothing to be fast about.
- **"The combat preview is tiny, the client can compute it."** ADR-013. It cannot.
- **"Keep `Ship` around, we'll want kinematics for the replay."** The replay draws a log
  (ADR-013). The tree loses `Ship` at step 1.
- **"A `MoveFleet` that takes a coordinate for convenience."** Two ids, always (ADR-015).
- **"The wall clock in the resolver, just to stamp the digest."** The tick stamps it. The server
  knows what time the tick was.
- **"Skip the harness, we have a server now."** The harness is what every session after this one
  tests with in thirty seconds. Build it first in step 6.
- **A third-party JSON library for the rules file.** R14. The rules file is the wire format.

## 6. Open questions this plan leaves to the sessions

- Everything the one-pager does not quantify: production per building per tick, fleet strength
  per ship, what a shipyard builds and how fast, mining station yield, the starting garrison, how
  fast a custodian's garrison decays. Decide, put it in `Rules`, and list the decisions in the
  step 2 report as candidate ADRs. Phase 0 tunes them.
- What research is. A do-nothing phase until the design says.
- Whether a fleet can be split or merged. The one-pager never says; the resolver treats a fleet as
  a unit until the design says otherwise, and the step 2 report flags it.
- The generator's rejection rate at twelve seats (step 1) and the snapshot size (step 3), both to
  be measured, not guessed.
