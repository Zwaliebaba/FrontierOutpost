# MVP-02 — The loop, in eight slices

**Status:** Plan, not started. Owner decisions recorded 2026-09-10 in two rounds; §2 is the complete list and ADR-003 to ADR-018 are the record. Nothing in this plan is built.
**Read first:** [`AGENTS.md`](../../AGENTS.md) in full, then [`Design/README.md`](../README.md) §1, then [`space-4x-one-pager-v10.md`](../space-4x-one-pager-v10.md) and [`space-4x-prototype-test-plan.md`](../space-4x-prototype-test-plan.md), then the ADRs your slice touches. This plan does not repeat any of them.

The target is the loop the test plan's Phase 0 runs on: a match of eight seats, resolved on a
schedule, on a map worth looking at. **It is deliberately not one build session.** It is eight
slices, each of which ends with something you can run and look at, and after each of which the
direction can change without throwing work away. A slice that turns out wrong costs one slice.

Read §5 before starting any slice. It is the list of ways this plan gets ruined.

---

## 1. What "done" looks like, at the end of slice 8

`FrontierServer.exe` creates a match from a seed for eight seats on a one-hour schedule, prints
eight tokens, and runs unattended. Eight clients present a token each, see the same generated
galaxy as a 640×400 sixteen-colour isometric diorama, place fleet, build and proposal orders that
lock together at the hour, and after every lock read a digest of what changed and a map on which
their fleets have visibly moved. A capital cannot be attacked for its first twelve ticks and says
so. A seat that has not logged in for three ticks is a custodian on everyone's map with a decaying
garrison. A trade lane is proposed from the build menu, accepted from the digest, and pays from the
tick it was accepted at. The match ends on the tick it said it would, with placements. The server
was restarted in the middle and nobody noticed.

**Not in this plan at all:** Exile, the sealed region's contents, colony cores, upkeep, salvage,
raiding, hiring, accounts, season rank. The sealed region is generated and visible from tick one
because the one-pager says so, and nothing can enter it. Also not in this plan, and named in
ADR-015 as the first work after the Phase 0 gate: the system close-up view, the combat replay, and
the timeline scrubber.

## 2. Decisions already made (do not reopen these)

| | Decision | Where it binds |
|---|---|---|
| **The game** | The one-pager, v10. The MVP-01 real-time ship was a proof of the stack, not the game. | Owner, 2026-09-10 |
| **Platform** | The Windows D3D12 640×400 client stays the client. Mobile is a later port and a later ADR. | Owner, 2026-09-10; `Design/README.md` §1 |
| **Process shape** | `FrontierServer.exe` headless; `FrontierOutpost.exe` the client and, with no arguments, the harness hosting every seat in-process. | ADR-011 |
| **Persistence** | One snapshot file per tick plus the pending books, in a match directory beside the server. The client still ships alone. | ADR-012 |
| **Preview and replay** | Computed by the server from visible information; the digest carries the round log. The client holds no rules. | ADR-013 |
| **World and tick** | A graph with authored lane costs; a pure resolver in six phases; integers with a stated remainder rule; sub-phase 4a a `Rules` flag, off. | ADR-004 |
| **Schedule** | Fixed UTC times as match data. Four a day by design, one hour for Phase 0. The resolver reads no clock. | ADR-007 |
| **Replication** | A per-seat visible snapshot and a per-seat digest after every tick, complete, never a delta. | ADR-005 |
| **Transport** | Reliable, ordered, framed, versioned messages over TCP; every request answered; loopback carries the same frames. | ADR-006 |
| **Map layout** | The generator emits display coordinates; drawn lane length is monotone in tick cost; unplaceable seeds rejected. | ADR-014 |
| **The scene** | A 3D diorama of authoritative state; cosmetic time between ticks; orders are ids, never a coordinate. Digest first, map second. | ADR-015 |
| **Camera** | 2:1 dimetric in whole pixels, snapped, panning over bounded map coordinates; zoom about the point under the pointer. | ADR-003, ADR-008 |
| **Identity** | A token per seat; presenting it is logging in. No accounts before Phase 1 gates. | ADR-016 |
| **Fog** | Topology and lane costs public; a system's yield, owner, buildings and resident fleets fogged until observed, then remembered as stale with a tick stamp. Fleets in transit are public. | ADR-017 |
| **Fleets** | A fleet is an owner, a location and a divisible integer strength. Split is an order; same-owner fleets at one system coalesce at end of tick. A garrison is a pinned fleet. | ADR-018 |
| **Income** | **One scalar.** Everything the one-pager says about economy is a single number: lanes pay +X/tick, the income screen shows income foregone, custodian conquests yield half. Buildings and fleet strength are bought with it. | Owner, 2026-09-10 |
| **Supply cost** | A `Rules` field defaulting to **zero**. Lane cost does the anti-snowball work in these slices; Phase 0 can switch upkeep on without a code change. | Owner, 2026-09-10 |
| **Slice order** | Breadth first. The galaxy is on screen at slice 2, before a single rule is built on an unmeasured galaxy size. | Owner, 2026-09-10 |
| **Documents** | The one-pager's text is current; the test plan's "message" instrument was stale. Both corrected 2026-09-10. | Owner, 2026-09-10 |
| **Presentation, palette, shading, starfield, pointer input** | Unchanged from MVP-01. | ADR-001, ADR-002, ADR-009, ADR-010 |

## 3. How a slice is verified

**The sessions writing this code have no Windows toolchain** (owner decision, 2026-09-10: write
here, compile on CI). So the loop is:

1. Write the code, and locally run what a Linux box can: `python Build/CheckFormat.py`, and
   `clang-tidy` and a scratch `clang++ -fsyntax-only` pass over the files that do not touch the
   Windows SDK.
2. Commit and push the branch. [`.github/workflows/build.yml`](../../.github/workflows/build.yml)
   runs on every push to an open pull request, and there is one for this branch, so the push
   alone starts the build. With no pull request open, dispatch the workflow against the branch
   instead — it carries `workflow_dispatch` for exactly that. Do not do both: two runs of a
   thirty-minute Windows build for one commit is waste, and the second was cancelled on
   2026-09-10 after this was found out the hard way.
3. Read the run. That run **is** the build: `CheckProjectFiles.py`, Debug|x64, the four test
   suites, and whole-tree `RunClangTidy.py`, on Windows. Fix and push again until it is green.
   A slice is not finished on a red run.

**What this loop cannot verify, ever:** that the game draws, that a tap lands, that the map is
legible, that the motion reads well. `AGENTS.md` §3 requires the executable to be **run** for
anything touching rendering, input or presentation, and CI cannot run it. So every slice from 2
onward ends with a **run list**: the specific things a human must launch the executable and look at,
written as a checklist in the slice's report. Until somebody works that list, the slice's rendering
claims are "builds and tests pass, not run", and the report says exactly that.

`Build/CheckProjectFiles.py` reports false failures on Linux because it compares Windows path
spellings. Do not fix it for Linux and do not trust it locally; CI runs it where it is correct.

## 4. What the tree loses, and in which slice

Say it before building, so nobody preserves it by reflex. Until each deletion lands, the comments
in those files cite ADR-004, ADR-005 and ADR-006 for decisions those files no longer record. That
is fixed by deletion, not by editing comments.

| Slice | Deleted |
|---|---|
| 1 | `GameLogic/Ship.{h,cpp}`; `ShipKinematicsTests` entire; `WorldTests` rewritten |
| 2 | `Neuron::MoveToOrder`, `Neuron::ShipState` and their sizes, serializers and tests; `FrontierOutpost/ShipView.{h,cpp}`; `Session::TICKS_PER_SECOND`, `TICK_MICROSECONDS`, `TheTickRateIsTwentyHertz`; the status line's X and Z |

The `Write`/`Read` cursor helpers in `Protocol.cpp` stay — they are the format (ADR-012). If a
client-side heading is wanted for a mesh it is a `float` in the client, where R16 does not reach.

**`NeuronCore/Trigonometry.{h,cpp}` stays, and this plan was wrong to list it for deletion.**
Slice 1 found the consumer while building the generator: placing capitals and rings on a circle
needs a sine and a cosine, and R16 forbids `std::sin` inside `GameLogic`, so `SineCosineTurns16` —
integer CORDIC, already written and already tested to 19 parts in 65536 — is exactly the right tool
and is now called from `Galaxy.cpp`. `Turns16`, `TRIG_ONE` and `TrigonometryTests` stay with it.
`Atan2Turns16`, `IntegerSquareRoot` and `SaturatingAdd` have no caller today and are kept rather
than picked off one at a time; a later slice may take them out together.

## 5. Things that will ruin this plan, and the answer

- **"I'll do slices 1 and 2 together, they're both small."** No. Slice 2 exists to be *looked at*
  before rules are built on the galaxy size it measures. Landing it with slice 1 means nobody looks.
- **"I'll let the client move the ghost when the order is accepted."** No. The ghost is drawn from
  the acknowledged book and stays a ghost until a snapshot says otherwise (ADR-015).
- **"The resolver would be simpler if this phase updated the fleet in place."** It would, and then
  the no-read rule is a hope. Immutable in, fresh out (ADR-004).
- **"A `float` for the yield fraction."** R16. Integers, and the remainder rule.
- **"An `unordered_map` keyed by id, it's faster."** R16 forbids it in `GameLogic` outright, and
  there is nothing to be fast about at four ticks a day.
- **"The client can compute the preview, it's tiny."** ADR-013. It cannot.
- **"Keep `Ship`, we'll want kinematics for the replay."** The replay draws a log. `Ship` goes in
  slice 1.
- **"A `MoveFleet` that takes a coordinate, just for the harness."** Ids, always (ADR-015).
- **"The wall clock in the resolver, just to stamp the digest."** The tick stamps it.
- **"CI is green, so it works."** CI cannot see the screen. Work the run list (§3).
- **A third-party JSON library for the rules file.** R14. The rules file is the wire format.

---

## 6. The slices

Every slice ends green on a dispatched CI run, updates `Design/` in the same commit as the code it
describes, and reports per `AGENTS.md` §7 and `Design/README.md` §6 — including which claims were
verified by CI and which are waiting on the run list. Commit at slice boundaries and at natural
points within them.

### Slice 0 — Nothing to build

Read the documents §0 names and the code §4 lists. Report what is there and where this plan
contradicts an ADR or the tree, and stop if it does. **Delivered on 2026-09-10**: the tree was
read, the split-fleet contradiction was found and fixed by ADR-018, and the three unstated shape
rules were answered by ADR-017, ADR-018 and §2's income and supply-cost rows.

### Slice 1 — The galaxy exists

**`GameLogic` only. Nothing else in the tree is touched.**

`MatchState`, `Rules`, the id types, and `Generate(seed, seatCount, rules)` per ADR-004, ADR-014,
ADR-017 and ADR-018. Every entity has a stable integer id; every container is a `std::vector`
ordered by id or a `std::map`; there is no unordered container in the library and
`CheckProjectFiles.py` gains a check that fails one in `GameLogic/`. Every quantity is an integer
carrying its unit. A text dump of a generated galaxy, for human eyes, in the test output.

`World` becomes a thin owner of a `MatchState` and `Ship` is deleted.

**Tests, and this is the suite that matters most for the rest of the game's life:** the generator's
guarantees (each capital a rival capital within three ticks by shortest path, one-tick lanes inside
a starting cluster, two- to four-tick lanes toward the frontier); rejection of a seed that fails
one; the layout constraint walked over every pair of lanes (higher cost never drawn shorter, no two
crossing, minimum separation); the sealed region generated, marked and reachable; capitals with a
pinned garrison; and the same seed producing byte-identical state twice.

**Measure and record:** the rejection rate over a thousand seeds at 6, 8 and 12 seats (ADR-014
needs the figure). If it is near one at twelve seats, **stop and report** rather than tuning.

**Not in this slice:** the resolver, any order, any protocol change, anything that draws.

**Run list:** none. Nothing draws yet.

**Delivered 2026-09-10.** The state, the rules, `Random` (ADR-019), the generator and the graph
queries; `Ship` and its nineteen kinematics tests deleted; `World` reduced to a holder;
`CheckProjectFiles.py` extended so that an unordered container or a floating-point type anywhere in
`GameLogic` fails the build, which is R16 stated as a check rather than as a hope.

The geometry was tuned against a prototype of the same integer arithmetic before any C++ was
written, because this container has no Windows toolchain and tuning blind would have cost a CI run
per attempt. Two of the four constraints were being satisfied by luck at first and are now
satisfied by construction: a lane's cost is read off its drawn length, so ADR-014's monotonicity
cannot fail, and the rings are concentric with lanes joining only angular neighbors or running
radially, so no two can cross. The prototype measured **0 rejections in 1000 seeds at each of 6, 8
and 12 seats**; `TheRejectionRateIsLowAtEverySeatCount` measures the real generator on every CI run
and logs the figure.

**One figure slice 2 starts from.** The prototype's eight-seat galaxy spans about 250 map units on
each axis. ADR-003 projects that onto roughly 4000 by 2000 virtual pixels at the default zoom,
which is six 640×400 screens across, and about three at the smallest zoom level the camera has. So
the galaxy does not fit on one screen at any zoom today, and slice 2's legibility measurement
should expect to move the default zoom, add a sixth level (ADR-008's open question), or scale the
map down in `Rules`. All three are data rather than code, which is the point of ADR-004 putting
them there.

### Slice 2 — The galaxy on screen

**The judgement slice.** Its purpose is to make the map lookable-at before any rule depends on how
big a galaxy can be.

`NeuronCore`: the frame, the version, and the `VisibleSnapshot` record only (ADR-006). The
`Simulation` seam narrowed to `Snapshot(seat)` and a `ResolveTick()` that does nothing yet;
`Session` gains `ResolveNow()` and loses its 20 Hz timer. Deletions per §4.

`GameLogic`: the visibility filter of ADR-017 — observed, known-and-stale with a tick stamp, and
unknown — and the snapshot it produces. Tested hard: a seat that leaves a system keeps its yield
and loses its fleet count; the stamp is the tick of last observation; **a seat's snapshot never
contains another seat's anything**.

`NeuronClient`: `Follow()` becomes `LookAt()` with map bounds and a clamp (ADR-003); `ZoomBy`
takes an anchor (ADR-008); picking against projected bounds; drag in `PointerInput`; a thin-quad
line path for lanes. Exact tests for the picking and the anchor, as `IsometricCameraTests` does
today.

`FrontierOutpost`: the harness of ADR-011 hosting one seat in-process, and `Frontier::Scene` per
ADR-015 built from the snapshot alone. Systems drawn as meshes in the family of `StationMesh.h`,
lanes as quads with their tick cost legible, the three fog states visually distinct, the sealed
region marked with its countdown.

**Measure and record:** how many systems are on screen at each zoom level on a generated eight-seat
galaxy, and whether a system can be reliably tapped at the zoom where one empire fits (ADR-015's
open question). **The galaxy size in `Rules` is tuned after this figure exists, not before.**

**Not in this slice:** any order, any resolver phase, fleets, the digest, the schedule, TCP.

**Run list:** launch it; pan the whole galaxy at every zoom; tap ten systems and confirm the right
one selects; confirm the fog states read as different at a glance; confirm no crawl or shimmer while
panning; read the legibility numbers off the screen.

### Slice 3 — Fleets move

`GameLogic`: the lock phase and the movement phase, and nothing else. `MoveFleet(fleetId, systemId)`
and `SplitFleet(fleetId, strength)`; end-of-tick coalescing (ADR-018); a per-seat order book that
is editable until the lock and hidden from every other seat.

`NeuronCore`: the order messages, the `Verdict` and its reason codes.

`NeuronServer`: `Submit`, `Withdraw` and `OrderBook(seat)` answered per seat.

`FrontierOutpost`: fleet meshes; fleets in transit at their cosmetic-time position with a tick-ETA
label (ADR-015); the seat's own pending orders as unlit ghosts on dotted routes; an orders screen
with its fleets column; a seat selector and a *Resolve now* target in the harness.

**Tests:** a fleet ordered out the tick a hostile arrives escapes; a fleet arrives on the tick its
lane cost says; a split refused at zero and at full strength; coalescing takes the lower id; an
order submitted after the lock lands in the next book; the cosmetic-time position is a pure
function of the two ticks and the clock, tested at the ends and past both.

**Not in this slice:** combat, claims, production, proposals.

**Run list:** launch the harness; select a fleet, tap a system, see the ghost; *Resolve now*; watch
the fleet on the lane with its ETA; wait and watch it advance without a resolve; resolve until it
arrives; split a fleet and send half.

### Slice 4 — Territory changes hands

`GameLogic`: combat sub-phase 4b with the incumbent bonus, no bonus for simultaneous arrivals at an
empty system, ties as mutual attrition, integer proportional damage with the largest-remainder rule;
sub-phase 4a written and behind its `Rules` flag, off; claims and captures on the survivor snapshot;
the two-tick siege. The `EngagementLog` (ADR-013) and the `Digest` sorted by consequence for these
events.

`FrontierOutpost`: the digest as the **opening screen**, each entry a tap target that calls
`LookAt()` (ADR-015); ownership on the map.

**Tests, one per sentence the one-pager writes:** two surviving hostiles leave a system occupied and
unclaimed; a fleet that arrives and dies contests nothing; a capture needs two consecutive
uncontested ticks; **the resolver's output is byte-identical under every permutation of the order
books**; with 4a on, the departing fleet takes one round computed from the arrivals'
end-of-movement strength.

**Not in this slice:** production, proposals, player states, the combat replay animation.

**Run list:** two seats in the harness contest one system; resolve; read both digests and confirm
each explains what that seat could see; tap a digest entry and confirm the camera goes to the right
place; take a system after a siege.

### Slice 5 — The economy

`GameLogic`: the production phase; one scalar income; buildings as unlocks with a cost in income, a
build time in ticks and a yield per tick (shipyard and mining station); fleet strength bought at a
shipyard; the research phase present and doing nothing, named as such.

`FrontierOutpost`: the build menu on a selected system; the income screen showing income foregone
the way the one-pager describes.

**Tests:** income accrues per tick from held systems and buildings; a build completes on the tick
its time says; a build refused when income is short; conquered systems yield what the rules say.

**Not in this slice:** trade lanes, which are diplomacy.

**Run list:** build a shipyard; watch income change on the income screen; buy strength; confirm the
greyed *Propose* entries appear in the menu even though they do nothing yet.

### Slice 6 — Diplomacy

`GameLogic`: proposals with the one-pager's whole lifecycle — locking with the other orders, four
ticks open, withdrawable while open, re-validated at every lock and voided with a reason in both
digests, a conditional order so acceptance needs no second round trip, effect at the first lock
after acceptance, and *ignored* reported to the proposer after four ticks. Trade lanes as a
building with two owners, cancellable by either at any lock, auto-cancelled on losing an endpoint,
with the digest distinguishing *cancelled by partner* from *cancelled: system lost*. Shared
scouting lifting fog per ADR-017.

`FrontierOutpost`: the proposals column; *Trade lane with [neighbour]: +X/tick* greyed with
*Propose* where *Build* would be; the first-contact prompt; proposals in the digest as countdowns.

**Tests:** a lane accepted at a lock opens in that lock's phase 1 and pays from that tick; a
proposal whose endpoint changed hands is voided and both digests say why; withdrawal while open;
the ignored report at exactly four ticks; shared scouting lifting fog for two seats and nobody else.

**Run list:** propose from one seat, accept from another, resolve, read both digests, confirm the
lane pays; cancel it and confirm the tell; let one run out unanswered.

### Slice 7 — The match as a whole

`GameLogic`: the four player states and six transitions and nothing else; custodian by three ticks
of absence, reversible, with pinned-garrison decay and half yield forever on systems conquered from
one; concession as permanent, forfeiting score, never denying an attacker their prize; the
first-week custodian scoring nothing; the capital guard countdown; the score; the fixed end tick;
the dominance threshold held for N ticks.

`FrontierOutpost`: custodian flags reading "custodian since tick N"; guard countdowns on capitals;
the public score with the leader visible; the end-date countdown.

**Tests:** every one of the six transitions, and that no seventh exists; absence counted in ticks
from the last `Join` or accepted order; a first-week custodian scores zero; the dominance threshold
does not end the match unless held; the match ends on its stated tick with a total ordering of
placements.

**Run list:** resolve a whole short match to its end tick in the harness; read the placements;
confirm a seat left idle becomes a custodian and can resume.

### Slice 8 — Real time and real players

`NeuronServer`: the `TickSchedule` in UTC against `system_clock`, driven in tests by an injected
clock; `Save`/`Load` and the match directory with `Pending.bin` on every accepted edit (ADR-012);
the event log the test plan names, as one append-only file; seat tokens (ADR-016).

`NeuronCore`: `TcpTransport` against Winsock, tested with two ends in one process.

`FrontierServer/`: the new project (ADR-011), registered in the `.slnx`, in
`Build/CheckProjectFiles.py` and in CI, with `AGENTS.md` §2's map and graph updated in the same
commit. Create and run subcommands; prints the tokens.

`FrontierOutpost`: connecting to an address with a token.

**Tests:** the schedule fires at the right UTC times under the injected clock; a restart from a
match directory resumes at the right tick with the pending books intact; a `Join` with a bad token
is refused; a seat receives its own snapshot and never another's over TCP.

**Run list:** create an eight-seat match on a one-minute schedule; watch three ticks resolve and
three snapshot files appear; kill the server and restart it and confirm it resumes; connect two
clients from two machines; confirm the event log holds every event the test plan names.

### Then — Phase 0

The test plan takes over. Eight clients against `FrontierServer.exe` on a one-hour schedule, the
harness not counting. Then, per ADR-015, the system close-up and the combat replay, which are cheap
once the `EngagementLog` and the meshes exist.

---

## 7. Open questions these slices must answer

Every one of these is a quantity or a rule the one-pager does not state. Decide it, put it in
`Rules`, and list the decision in the slice report as a candidate ADR; Phase 0 tunes the numbers.

- **Slice 1:** galaxy size per seat, cluster size, lane cost distribution, minimum system
  separation, the drawn-length-to-cost relation, starting income, the starting garrison's strength,
  where the sealed region is placed and how evenly (ADR-014's open question).
- **Slice 4:** how many combat rounds, what fraction of its strength a fleet deals per round, and
  the magnitude of the incumbent defender bonus. These three decide whether combat is decisive or
  grinding, and they are the most consequential unstated numbers in the design.
- **Slice 5:** what a shipyard and a mining station cost, take and yield; what strength costs; what
  research is, if anything.
- **Slice 7:** the scoring formula, which must be a total order; the dominance threshold and its
  hold duration; the custodian decay rate.

Two things to note rather than decide. The capital guard is twelve ticks, which is three days of a
twenty-one day match and twelve hours of Phase 0's forty-eight, so Phase 0's tuning of guard length
does not transfer to Phase 1 proportionally. And ADR-017 leaves the scouting reveal radius as a
`Rules` value at one lane, which Phase 0 can set to zero.
