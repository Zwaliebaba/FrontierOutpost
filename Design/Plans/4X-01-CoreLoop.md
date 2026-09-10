# 4X-01 — The loop, headless: a galaxy, a tick, and everything that resolves in it

**Status:** Plan, in progress. Written 2026-09-10 against `space-4x-one-pager-v10.md` (v0.7) and
`space-4x-prototype-test-plan.md` (v0.4). Steps 0–4 done 2026-09-10.

This plan builds the *simulation* half of what the one-pager's *Build order* asks for: **"Loop
first — graph generator, tick resolution, trade lanes and proposals in the build menu, custodian,
capital guard, siege rule, digest, fixed end. No Exile, no region."** It stops when a match
resolves end to end, in one process, deterministically, with scripted players.

**It was one plan and is now two.** The server shell, the client wiring and the network are
`4X-02-ServerAndClient.md`, and that plan is deliberately a stub until this one lands: its steps
were specified against a snapshot format and message sizes that do not exist yet, and detail
written now is detail rewritten later. `Design/README.md` §2 says a Plan is expected to change
every week; writing three weeks ahead of the code is how it earns that reputation.

---

## 0. Where the tree is, and why that shapes the order

Read AGENTS.md and Design/README.md first, as always. Then know these five things about the tree
you are starting from, because the plan's order follows from them.

**The client exists and the game does not.** ADR-014 built the main page — digest, map, orders —
and it renders a fixture (`FrontierOutpost/MatchFixture.cpp`) that is the design reference's
tick 46, hand-typed. ADR-017 gave the map a real camera. Every interaction on that screen works
against local state and nothing behind it. `Frontier::MatchState` (`FrontierOutpost/MatchState.h`)
is the client's model of a match and is already shaped like the README's *State* section: match,
player, digest, graph, fleets, orders, proposals, region. **It is the decode target.** When the
server exists, the fixture is what it replaces.

**`GameLogic` and `NeuronServer` are empty on purpose.** ADR-015 removed the MVP-01 vertical slice
— the ship, its simulation, the 20 Hz session, the loopback transport and the two ship-shaped wire
records — and left both projects holding an umbrella header, a precompiled header and a
`SuiteSmoke` placeholder. They are where this plan's server-side work goes, and their test suites
are where its tests go. `NeuronCore` holds `Debug.h` and nothing else game-facing.

**Three deprecated ADRs are the reading list, not the starting point.** ADR-005 (replication),
ADR-006 (transport queues) and ADR-007 (the engine/game seam) were correct for a real-time demo and
say so plainly. Their reasoning about *why* an interface sits where it does still holds; their
specifics — 20 Hz, a 32-byte state, a 256-slot ring buffer — do not. §3 asks for their successors.

**R16 is the load-bearing rule.** `GameLogic` compiles `/fp:precise`, uses no `float` where an
integer will do, iterates no unordered container into the simulation, and reads no wall clock. For
this game that is not a constraint to work around; it is the design. The one-pager says combat is
"deterministic... integer arithmetic" and processing order "can never change an outcome". A
resolver that is a pure function of (state, orders, seed) is what makes the loop testable without a
network, replayable for the client's *Replay tick N*, and persistable as a seed and a list of
orders. §2 and §3 lean on it hard.

**R13 is going to be tested by this plan.** The executable ships alone, and "never add a runtime
file dependency" was written for assets. A match that runs for three weeks across four ticks a day
is not running in one process the whole time, and its state has to live somewhere between
restarts. That is a genuine collision with the letter of the rule and it is an owner decision,
listed first in §3.

---

## 1. What "done" looks like

A headless match runs from generation to a fixed end tick with **six scripted players**, entirely
in-process, deterministically: the same seed and the same order sets produce byte-identical state
at every tick, asserted by a hash in `GameLogicTests`. Every rule in the one-pager's loop is
implemented, parameterised, and has a scenario test that would fail if it were removed. The rule
checklist in §7 is complete with a step number and a test name against every row.

There is **no server, no network and no persistence** in this plan, and the client is wired only as
far as each step's own visible check allows — Step 2 points the existing map at a generated galaxy
because it costs almost nothing and proves the design-space mapping, and that is the extent of it.
`4X-02` does the rest.

Not in "done": Exile, the sealed region's *rules*, raiding, salvage, the runway, hiring, accounts,
matchmaking, a mobile client, chat. The region is *drawn* — the generator places it and the client
already renders it — but nothing happens when it opens. That is the one-pager's build order, and
§5 says what to do when it tempts you.

---

## 2. Decisions already made (do not reopen these)

From the one-pager, which is the design authority for the game:

| | Decision | Source |
|---|---|---|
| **World** | A bounded graph: systems are nodes, lanes are edges with an authored integer tick cost. No coordinates in the *simulation*; positions exist only for drawing. | One-pager, *Shape of a game*, *What it is not* |
| **Generator** | Sized to player count (6–8 prototype, 12 by design). Each capital has a rival capital within three ticks; one-tick lanes inside starting clusters; two-to-four-tick lanes toward the frontier; a seed that cannot satisfy this is rejected. | *Shape of a game* |
| **Time** | Tick-quantised. Four ticks a day at fixed UTC in production; the tick interval is a **match parameter** because Phase 0 runs a one-hour tick and Phase 1 a six-hour one. Nothing happens between ticks. | *Shape of a game*; test plan |
| **Orders** | Hidden until lock. Editable any time until then. Fleets, builds and proposals lock together. | *Shape of a game*, *Diplomacy UI* |
| **Resolution** | Six phases in fixed order: lock, production and research, movement, combat (4a rear-guard *built and switched off*, 4b system combat), claims and captures, digest. Every phase reads the tick-start state and writes the next; no order reads another's write within a phase. | *Tick resolution* |
| **Combat** | Deterministic. Integer arithmetic. Fixed rounds. Damage spread across enemies in proportion to strength. Incumbent gets the defender bonus; simultaneous arrivals at an empty system get none. A tie is mutual attrition. Combat only at systems; fleets pass on lanes. Movement precedes combat, so leaving beats arriving. | *Tick resolution* |
| **Claims** | Claim requires presence uncontested by any surviving hostile at end of tick. Capture of an owned system needs two consecutive ticks uncontested — siege, then capture. A fleet that arrives and dies contests nothing. | *Tick resolution* §5 |
| **Trade lane** | A building with two owners, in the build menu with *Propose* where *Build* would be. Pays more than any internal lane. Either party cancels at any tick. Opens in phase 1 of the lock it is accepted at and pays that tick. Cancels automatically when an endpoint changes hands; the digest says *cancelled by partner* or *cancelled: system lost*, never just *cancelled*. | *Decision three*, *Diplomacy UI* |
| **Proposals** | An order. Locks with the others, arrives in the next digest, stays open four ticks, withdrawable while open, takes effect at the first lock after acceptance, may carry a conditional order. Re-validated at every lock; one that fails is voided and both digests say why. Unanswered after four ticks → reported to proposer as *ignored*. Three kinds: open a lane, share scouting, hold for N ticks. First contact raises the game's own prompt. | *Decision three*, *Diplomacy UI* |
| **Digest** | One per tick, never one per event. Sorted by consequence. The primary screen. | *Core loop* |
| **Player states** | Active, Custodian, Exile, Gone; six transitions. Custodian by three ticks of absence (reversible) or concession (permanent). Custodian territory defends, never expands or attacks; flagged on every map as "custodian since tick N"; garrisons weaken each tick of absence; systems conquered from a custodian yield half for the rest of the match; a first-week custodian scores nothing. **Exile and Gone are Phase 2.** | *Player states* |
| **Capital guard** | Capitals cannot be attacked for the first twelve ticks. Visible countdown. Independent of the siege rule. | *Pacing devices* |
| **Scoring** | Public; the leader is always visible. Fixed end date known at start. Placement by score. Early dominance threshold ends the match only if held for several consecutive ticks. | *Pacing devices*, *Scoring between matches* |
| **No free text** | v1 has no chat and no message field. Diplomacy is buttons. | *What it is not* |
| **Watch item** | Sub-phase 4a exists in code, switched off, enabled only if Phase 0 shows dancing dominates. | *Tick resolution*; test plan Phase 0 |

From the tree, which is the conformance authority:

| | Decision | Source |
|---|---|---|
| **Determinism** | `GameLogic` is integer-only, `/fp:precise`, no wall clock, no unordered iteration into the simulation. | AGENTS.md R16 |
| **Layers** | `NeuronCore` is shared engine; `NeuronServer` is server-only engine; `GameLogic` is the game, server-side, referenced by the executable and nothing else. The client never links it. | AGENTS.md §2, R9 |
| **The client's model** | `Frontier::MatchState` is the client's view of a match and holds no rules. | ADR-014; `MatchState.h` |
| **The screen** | 1280×720 R8G8B8A8, one 8×8 font, the ops console, a perspective orbit camera. | ADR-011, ADR-014, ADR-017 |
| **What was removed** | The MVP-01 slice is gone and stays gone; `GameLogic` and `NeuronServer` are the empty projects this work fills. | ADR-015 |

---

## 3. What you must decide, and record as ADRs

Each of these has alternatives that are genuinely available, so each is an ADR (Design/README.md
§2). The first is an owner decision and blocks Stage B; the rest are decisions the session takes
and records, in the step that meets them. A recommendation is given where the plan has one, so the
session can say "as recommended" rather than re-derive it — but *say why the other loses*.

**ADR-018 — Determinism: the simulation is a pure function.** *(Step 0. Written.)* The resolver is
a pure function of (state, orders) and the generator a pure function of (rules, seed); the PRNG is
pinned by value forever; every iteration that reaches a result is over a sorted order. It is R16
made load-bearing rather than merely observed, and it is what later lets a match persist as a seed
and a list of orders — a payoff `4X-02` collects and this plan only makes possible.

**ADR — Visibility.** What a player sees. The fixture already assumes fog ("41 systems" on a map
showing eleven). Decide the rule: own systems, systems one lane away, systems a fleet is at or
adjacent to, everything a scouting-share partner sees; fleets in transit are public once departed
(one-pager). Decide whether a system once seen stays known (last-seen owner) or goes dark.
*Recommendation:* last-seen with a tick stamp, so the map can grey it and the digest can say
"as of T41".

**ADR — Combat arithmetic.** *(Step 5.)* The one-pager gives the shape and no numbers: rounds, the
damage formula, what "strength" is, the defender bonus, what a tie means numerically. Every one of
these is a `MatchRules` parameter with an initial value, tuned in Phase 0. The ADR records the
shape — integer, proportional spread, fixed rounds, deterministic — and the initial parameters, and
says they are initial. *Do not* pick numbers that only work for the fixture's "14 v 11 (+def) →
6 left".

**Deferred to `4X-02`:** the seam, persistence and what R13 means for a server, time and the
schedule, transport, identity. Each is specified against something this plan has not built yet.

**Possibly an ADR — Where the combat preview is computed.** The orders rail shows "preview:
14 v 11 (+def) · 6 left". It is a pure function of visible information, so the client *could*
compute it — but the client does not link `GameLogic` (§2). *Recommendation:* the server computes
previews into the per-player snapshot. The client stays dumb and the seam stays clean. If that
turns out to want a round trip the design cannot afford, revisit; it will not.

---

## 4. The work, in order

Four stages. **Stage A** is the game, headless, deterministic, tested — most of the work and all
of the risk. **Stage B** is the server shell around it. **Stage C** wires the client. **Stage D**
puts it on a network so Phase 0 can run. Nothing in B–D changes a rule; if it wants to, that is a
finding, and it goes back to A.

Every step names where its code goes (AGENTS.md §2) and which suite tests it. The three suites
that hold a `SuiteSmoke` today — `GameLogicTests`, `NeuronCoreTests`, `NeuronServerTests` — lose
it at the first real test in each, and not before.

### Stage A — the loop, in-process

#### Step 0 — Read, then decide out loud

Read the one-pager, the test plan, AGENTS.md, Design/README.md §1, ADR-014 through ADR-017, and
the *Decision* sections of ADR-005, -006, -007. Read `MatchState.h` — it is the shape a snapshot
has to fill.

Then write **one** ADR: determinism. It is the only one Stage A needs before code exists, because
it constrains the PRNG and the iteration order that Steps 1 and 2 are built on. The rest are
written **in the step that meets them** — visibility at Step 8, combat arithmetic at Step 5 — which
is what §3 says and what an earlier draft of this step contradicted by asking for five up front.
An ADR about combat written before a resolver exists is design ahead of the code, and
`Design/README.md` §5 is explicit about which way that dependency runs.

State your understanding of the six phases in your own words in your first report, with the
no-read rule and the movement-before-combat consequence. If that paragraph is wrong, everything
after it is.

#### Step 1 — The vocabulary, and the two things everything needs

`NeuronCore`: a strongly-typed integer id (`Neuron::Id<Tag>`) so that a lane index cannot be passed
where a system index goes — the *template* is engine, the tags are game and live in `GameLogic`
(R9); and a deterministic PRNG (`Neuron::Prng`, seeded, integer-only). The PRNG's *fixedness* is
the point, not its choice: its output must be identical on every machine forever, which rules out
`std::mt19937`'s distribution helpers, whose mapping the standard leaves implementation-defined.
**Tests in `NeuronCoreTests`:** the PRNG's sequence pinned by value from a known seed, and its
bounded draw shown unbiased and in range.

**Serialization is deferred to Step 3**, where orders first need it. Nothing in Steps 1–2 crosses
a wire or a file, and a byte writer built here would be a layer added before a second thing needed
it (R2).

`GameLogic`: `MatchRules` — every tunable the one-pager leaves open, as a struct with initial
values: player count, tick interval, match length in ticks, capital guard ticks (12), proposal
window (4), custodian absence (3), siege ticks (2), trade lane yield, lane cost ranges by zone,
combat parameters, dominance threshold and hold ticks, the half-yield rule. Phase 0 exists to
change these numbers; they are data from the first line.

**Done 2026-09-10, and `MatchRules` is narrower than the list above.** It holds what generation
needs, the four numbers the one-pager states outright, and the region's two. Trade lane yield,
combat parameters, the dominance threshold and the half-yield rule are **not** there yet: each
arrives in the step that first reads it — combat at Step 5, lane yield at Step 6, scoring at Step 7.

The list above is still the right end state and it is what Step 10 checks against. What it got
wrong is the timing. A field nothing reads cannot be tuned by Phase 0 and cannot be tested, so its
initial value is a guess that has acquired a home and looks settled; the struct's own comment says
it grows with the plan. If a later step finds a number it needs and no field for it, that is this
note working, not a defect.

#### Step 2 — The galaxy

`GameLogic/Galaxy` and `GameLogic/GalaxyGenerator`. A generator that takes `MatchRules` and a
seed and returns a graph satisfying the constraints in §2, or reports that the seed cannot — and a
driver that tries seeds until one does, recording the accepted seed as the match's. Positions are
integers in the design space the client already draws (800×560 units; `MapView` maps them), so
the client needs no change to show a generated galaxy. Place the sealed region's anchor and its
opening tick; attach no rules to it.

**Tests (`GameLogicTests`):** for a range of seeds and each player count 6–12 — every capital has
a rival capital within three ticks by shortest path; every lane inside a starting cluster costs
one; every lane on the frontier costs two to four; the graph is connected; the same seed produces
the same graph, asserted by hash. A rejected seed is *rejected*, not silently repaired. Record the
constraint numbers in `Design/Reference/galaxy-generation.md`, because they are facts about the
generator rather than decisions.

**Done 2026-09-10**, and three things happened that this step did not ask for. They are recorded
here rather than in the step's prose above, so that what was planned and what was built stay
distinguishable.

`NeuronCore/Turns16.h` was added: integer angles and an integer sine, because the ring is laid out
with trigonometry and `std::cos` is not bit-identical between standard libraries, which ADR-018
forbids. Serialization, which Step 1 named, was **deferred to Step 3** — nothing in Steps 0–2
crosses a wire or a file, and a format written before it has a second reader is a format written
twice.

`FrontierOutpost/GeneratedMatch` was added and the executable now boots a **generated** galaxy
rather than the design-reference fixture. That was the point of doing it here: it de-risks the
design-space mapping before a rule exists, and it caught two numbers the screen was reporting
without holding — a dangling `- ENDS` and a hardcoded `38 AVAILABLE`, now `Orders::availableBuilds`.
`MakeReferenceMatch` is unchanged and still matches the drawing.

It also made one limitation visible rather than theoretical: `MatchState::Owner` has three players
and a neutral, and a generated galaxy has six to twelve, so five players currently share two
colours. Widening it is **Step 8**, and it is a design question about the token palette rather than
a mechanical one.

#### Step 3 — The match state and the orders

`GameLogic/Match` — the authoritative state: the galaxy, ownership, fleets, buildings, open
proposals, active lanes, per-player standing and presence, the current tick, the accepted seed.
`GameLogic/Orders` — an `OrderSet` per player: fleet moves and holds, builds, proposals (with
optional conditional order), answers, withdrawals, concession. **Validation** at lock: a move along
a lane that does not exist, a build the player cannot afford, an answer to a proposal that is not
addressed to them — each rejected with a reason that reaches the digest. Nothing is silently
dropped; the one-pager's "nobody is ever shown a dead offer as acceptable" is a rule about the
whole system.

**Tests:** validation of every order kind, valid and invalid; an `OrderSet` serialised and read
back; a `Match` hashed identically from the same construction twice.

**Done 2026-09-10.** `GameLogic/Match` holds the state and owns `Validate`, which is read twice on
purpose: the resolver applies it at the lock, and the client will be shown the same answer before
the lock, so there is one function deciding whether an order is real rather than two that agree
today. Sixteen rejection reasons, each with a sentence that reaches the digest.

The serialization deferred from Step 1 arrived here as `NeuronCore/ByteWriter` and
`NeuronCore/ByteReader`: little-endian by explicit shifting rather than by `memcpy`, so the format
is a decision rather than whatever the compiler laid out, and a read past the end sets a flag
instead of being undefined — these bytes come off a socket in `4X-02`, and a reader whose error
path is a crash is a denial of service with extra steps.

Two things the step did not name. `MatchRules::Check` was added, so that rules which contradict the
game they are rules for — a trade lane paying no more than an internal one, a siege of zero ticks
— are refused with a reason; `Match::Create` is fatal on it, and the server can ask before it has
built anything. And `MatchRules` grew the production and build numbers, because Step 4's phase 2
reads them; that is the growth the Step 1 note above predicted.

#### Step 4 — Resolution without combat: phases 1, 2, 3, 5, 6

`GameLogic/TickResolver`. Implement the phases in order, with combat as a no-op, and get the
no-read discipline right first: **every phase reads a `const` view of the tick-start state and
writes a new one.** If the resolver's signature makes it possible to read what another order wrote
in the same phase, the signature is wrong.

- Phase 1 — lock: apply order sets; re-validate every open proposal against the new state; void
  and record why.
- Phase 2 — production and research: per-system production, lane income (a trade lane pays more
  than any internal lane — that is the whole incentive, and it is a parameter), idle shipyards
  noted for the digest. Research is a counter with no effect in Stage A; §6.
- Phase 3 — movement: every fleet advances one tick of its lane's cost; fleets pass each other.
- Phase 5 — claims and captures: the presence and siege rules of §2; lane auto-cancel with the
  distinguished reason.
- Phase 6 — digest: per-player event list, sorted by consequence. The sort order is a decision:
  contact and loss above proposal above economy, or by a numeric severity each event carries.
  Record it.

Produce a `TickLog`: what happened in each phase, in order. It is what *Replay tick N* shows and
what tests assert against.

**Tests:** a fleet ordered out the same tick a hostile arrives is gone before combat would run
(the one-pager's central consequence, testable now with combat still a no-op: the hostile finds an
empty system); an uncontested claim succeeds; a capture takes exactly two consecutive uncontested
ticks and is reset by one contested tick; a lane cancels with *system lost* when an endpoint
changes hands; production and lane income sum to the digest's figure; the digest is sorted as
decided; and the hash test — resolve the same tick twice, get the same state.

**Done 2026-09-10**, and the no-read discipline is **ADR-019**: every phase is `Match(const
Match&)`, so within a phase every read is of the parameter and every write is to the local copy.
The signature is the rule, which is what the step asked for. Combat is a phase that runs and does
nothing rather than a phase that is missing, so the log a replay reads has had the same six entries
since the first tick ever resolved.

The digest's sort order is **ADR-020**: a numeric severity carried on each event, sorted descending
with a total tie-break on kind, system and lane. Ranking by event *kind* was rejected because
consequence is not a property of the kind — a border world and a capital are both `SystemLost`.

Not implemented here, and not claimed: research, which the phase list names. It is a counter with
no effect in Stage A, and a field nothing reads is a number that looks tuned and is not. It arrives
with whatever first reads it.

#### Step 5 — Combat

Phase 4b, then 4a behind a `MatchRules` flag that defaults to off. Integer arithmetic throughout:
each fleet's damage spread across enemies in proportion to their strength, a fixed number of
rounds, the incumbent's defender bonus, no bonus for simultaneous arrivals at an empty system, a
tie leaving both sides attrited. Deterministic: the order fleets are considered in is a sorted
order, never a container's.

**Tests:** the fixture's own numbers as one case among many — but do not tune to it; the
proportional spread against two and three enemies; defender bonus applied to the incumbent and
only the incumbent; two fleets arriving together at an empty system get none; a tie leaves both
alive and both smaller; movement-before-combat now proven with combat live; 4a off by default, and
when on, the departing fleet takes exactly one free round computed from the arrivals'
end-of-movement strength. And the hash.

#### Step 6 — Trade lanes and proposals

The full lifecycle from §2: propose (as a build-menu order), open for four ticks, withdraw, accept
or decline (as orders), take effect at the first lock after acceptance, conditional order applied
in the same lock, re-validation every lock with voiding and reasons on both sides, *ignored* after
four ticks, cancel by either party, auto-cancel on endpoint loss with the two distinguished
reasons, first-contact prompt raised when two players' systems first become adjacent. Share
scouting and hold-for-N are proposals too; share scouting changes visibility (Step 8), hold-for-N
is recorded and its breach reported — it is not enforced, because nothing is (one-pager: "no
enforced treaties").

**Tests:** one per lifecycle transition; the four-tick window counted in ticks not real time; a
proposal whose endpoint is captured between offer and answer is voided with the right reason in
*both* digests; an accepted lane pays in the tick it was accepted; cancel-by-partner and
system-lost produce different digest text.

#### Step 7 — Players: presence, custodian, guard, score, end

Presence: a player is present for tick N if the server saw them between lock N−1 and lock N (the
server tells the simulation; the simulation never asks a clock). Three absent ticks → custodian,
reversible on return; concession → custodian permanently. Custodian territory defends and never
expands or attacks — its queued orders are discarded at lock and its build menu is empty; garrisons
weaken each absent tick by a parameter; systems conquered from a custodian yield half for the rest
of the match whoever holds them; a player custodian in the first week (a parameter in ticks)
scores zero. Capital guard: capitals cannot be attacked for the first N ticks, and the digest and
map show the countdown. Score: public, computed each tick from what is held, the leader always
identified. End: at the fixed end tick, placement by score; or earlier when a player has held the
dominance threshold for the required consecutive ticks.

**Tests:** each of the six transitions the one-pager allows and none it does not (Exile and Gone
are unreachable in Stage A and a test says so); a custodian's orders discarded; half yield
persisting across a second capture; week-one custodian scoring zero; the guard blocking an attack
on tick 12 and permitting it on tick 13; dominance held for N−1 ticks not ending the match and for
N ending it; placement ties broken as decided (record the tiebreak).

#### Step 8 — Visibility, and the per-player snapshot

Implement the visibility ADR. `SnapshotFor(playerId)` produces exactly the fields
`Frontier::MatchState` has — systems the player can see with last-seen stamps, lanes, fleets in
transit, their own orders and the open proposals addressed to them, their standing, the region
anchor, the totals ("41 systems") from the authoritative count — plus the combat previews the
orders rail shows, computed here. `DigestFor(playerId, tick)`.

**Tests:** a player sees their own systems and their neighbours and not a system three lanes away;
a shared-scouting partner's view is added and removed with the agreement; a fleet becomes visible
to everyone on the tick it departs; a snapshot round-trips through serialization; the snapshot
contains nothing the player is not entitled to (assert on a system that should be hidden).

#### Step 9 — Six bots play a whole match

`GameLogicTests` gets a scripted-player harness: six policies (expand-near, expand-far, turtle,
raider, diplomat, absentee) driven for a full match of `MatchRules` length. Assert: the match ends;
the determinism hash matches across two runs; no invariant is violated on any tick (ownership
consistent, no negative stock, no fleet on a non-lane, no proposal open past its window); the
absentee becomes custodian on schedule; the diplomat's lanes open and pay. **Measure and record**
the per-tick resolution time here, because §3's persistence recommendation depends on it being
small.

This is the pre-Phase-0 gate. If the harness surfaces a rule interaction the one-pager did not
anticipate, that is a finding for the owner, not something to resolve in a comment.

#### Step 10 — Close out Stage A

Update this plan's status and the §7 checklist — every row needs a step and a test name, and a row
without one is a rule that was dropped, which the report says out loud. Confirm §3's ADRs exist.
Write `4X-02-ServerAndClient.md` properly now that the snapshot shape, the order-set shape and the
per-tick resolution cost are facts rather than guesses. Report per `Design/README.md` §6.

## 5. Things that will tempt you, and the answer

**A `float` in `GameLogic`.** No. Positions are integers in design units; production, strength,
damage and score are integers; proportional damage is integer multiply-then-divide with a stated
rounding rule. R16 is why the match can be a seed and a list.

**Reading the clock in the simulation.** No. Presence, the tick number and "how long until lock"
are things the server *tells* the simulation. `GameLogic` has no `std::chrono`.

**Building Exile or the region's rules "while you are in there".** No. The one-pager's build order
excludes them and the test plan gates them behind H1–H3. Place the anchor, draw it, stop.

**Turning 4a on because it is obviously right.** No. It is a Phase 0 watch item with a measured
trigger ("more than a third of the time it is targeted"). Build it, flag it off, leave the flag.

**A message field, a notification per event, a diplomacy tab.** No, no, no. The one-pager says
what it is not, and ADR-014 built the screen without them on purpose.

**Hard-coding four ticks a day.** No. Phase 0 is a one-hour tick. Everything the one-pager gives a
number to is a `MatchRules` field.

**Tuning combat until the fixture's "14 v 11 → 6 left" comes out.** No. The fixture is a drawing.
One test may use its numbers; the parameters are for Phase 0 to set.

**Letting the client compute anything the server is authoritative for.** The preview is the
temptation, because it is cheap and pure. The server computes it into the snapshot (§3). The
client never links `GameLogic` and the day it does, the server stopped being authoritative.

**Iterating an unordered container into a resolution.** No. Sort by id first. It is the
determinism bug that passes on one machine and fails on another.

**Game nouns in `NeuronCore`.** The wire records are the sanctioned exception, as ADR-007 recorded;
keep them to what has to cross, and put the *rules* in `GameLogic`. If `NeuronCore` grows a
function that knows what a capital is, it is in the wrong library.

**Starting with the network because it is the exciting part.** Stage A is the risk. A network in
front of a resolver that is not yet right is a very good way to test the network.

---

## 6. Open questions this plan leaves to the session

**What research does.** The one-pager names it in phase 2 and nowhere else. Stage A makes it a
counter that accrues and affects nothing, and reports it in the digest as the fixture does
("research +4"). What it unlocks is a design question for the owner.

**What a shipyard and a mining station do, and cost.** The fixture shows costs and yields; the
one-pager does not. Initial values go in `MatchRules`; the *effects* — a shipyard builds ships at
some rate, a mining station adds production — need a sentence each from the owner.

**What "strength" is.** Ship count is the obvious first answer and is probably right for Phase 0.
The combat ADR should say so and say it is provisional.

**Scoring formula.** "On what they hold at match end" — systems, or systems weighted by
production, or something else. Placement ties need a rule.

**The consequence sort.** Step 4 needs a total order over digest events. A numeric severity per
kind, with ties broken by tick of origin, is the recommendation; it is a decision.

**Whether the region opening does anything visible in Stage A.** The plan says no. A digest line
("Sealed region open. Nothing can enter it yet.") is honest and cheap and is the owner's call.

**Whether Phase 0's compressed clock needs a compressed guard.** Twelve ticks of capital guard is
three days at four a day and twelve hours at one an hour. The test plan says Phase 0 tunes it;
`MatchRules` makes that possible; the initial Phase 0 value is the owner's.

---

## 7. The rule checklist

Every rule the loop stage owes, with where it lands. A row without a step and a test at close-out
is a rule that was dropped, and the report says so.

| Rule | One-pager | Step | Test (to be named at close-out) |
|---|---|---|---|
| Graph of systems and lanes, integer costs | Shape | 2 | `GalaxyGraphTests` |
| Sized to player count 6–12 | Shape | 2 | `TheGalaxyIsTheSizeTheRulesAskFor`, `APlayerCountOutsideTheDesignIsRefusedBeforeAnythingIsBuilt` |
| Capital within 3 ticks of a rival capital | Shape | 2 | `EveryCapitalHasARivalWithinThreeTicks`, `CapitalsTooFarApartAreRefused` |
| 1-tick lanes in clusters; 2–4 toward frontier | Shape | 2 | `LaneCostsMatchTheTwoBands`, `AClusterLaneThatDoesNotCostOneIsRefused`, `AFrontierLaneOutsideTheBandIsRefused` |
| Unsatisfiable seed rejected | Shape | 2 | `GalaxyValidationTests`, one case per rejection |
| Tick interval a parameter; nothing between ticks | Shape; test plan | 1 | `MatchRulesTests` via `RulesThatContradictTheDesignAreRefused` |
| Orders hidden until lock; editable until then | Shape | 3 | `OrderValidationTests` |
| Fleets, builds, proposals lock together | Diplomacy UI | 3 | `ResolvingATickAdvancesItAndLogsSixPhases`, `ASecondOrderSetFromOnePlayerIsDiscarded` |
| Six phases, fixed order, no-read within a phase | Resolution | 4 | `ResolvingATickAdvancesItAndLogsSixPhases`, `ResolvingTheSameTickTwiceGivesTheSameState` (ADR-019) |
| Movement before combat: leaving beats arriving | Resolution | 4, 5 | `AFleetOrderedOutIsGoneBeforeTheHostileArrives` |
| Fleets pass on lanes; combat only at systems | Resolution | 4 | `ALongerLaneTakesItsFullCost` (partial: needs combat, step 5) |
| 4a rear-guard built, off by default, one free round | Resolution | 5 | |
| 4b melee: proportional, fixed rounds, integer | Resolution | 5 | |
| Defender bonus to incumbent; none for simultaneous arrival | Resolution | 5 | |
| Tie is mutual attrition | Resolution | 5 | |
| Claim requires uncontested presence at end of tick | Resolution §5 | 4 | `AnUncontestedFleetClaimsAnUnclaimedSystem`, `TwoRivalsInOneUnclaimedSystemClaimNothing` |
| Capture needs 2 consecutive uncontested ticks | Resolution §5 | 4 | `CaptureTakesTwoConsecutiveUncontestedTicks`, `OneContestedTickResetsTheSiege` |
| Arrive-and-die contests nothing | Resolution §5 | 5 | |
| Two surviving hostiles: occupied, unclaimed | Resolution §5 | 5 | |
| One digest per tick, sorted by consequence | Core loop | 4 | `TheDigestIsSortedByConsequence` (ADR-020) |
| Trade lane is a building with two owners, in the build menu | Decision 3 | 6 | |
| Lane pays more than any internal lane | Decision 3 | 4, 6 | `ATradeLanePaysBothSidesAndPaysMore`, `RulesThatContradictTheDesignAreRefused` |
| Either party cancels at any tick | Decision 3 | 6 | |
| Lane opens in phase 1 of the accepting lock, pays that tick | Decision 3 | 6 | |
| Auto-cancel on endpoint loss, two distinguished reasons | Decision 3 | 4, 6 | `ATradeLaneCancelsWhenAnEndpointChangesHands` (partial: *by partner* is step 6) |
| Proposal is an order; 4-tick window; withdrawable | Decision 3 | 6 | `AProposalArrivesInTheRecipientsDigest`, `AnUnansweredProposalIsReportedAsIgnored`, `AWithdrawnProposalTellsTheRecipient` |
| Effect at first lock after acceptance; conditional order | Decision 3 | 6 | `AcceptingALaneOpensItAndChargesTheProposer` (partial: conditional orders are step 6) |
| Re-validated every lock; voided with reason both sides | Decision 3 | 4, 6 | |
| Unanswered 4 ticks → *ignored* to proposer | Diplomacy UI | 6 | `AnUnansweredProposalIsReportedAsIgnored` |
| Three proposal kinds: lane, share scouting, hold N | Diplomacy UI | 6 | |
| First-contact prompt | Diplomacy UI | 6 | |
| No free text | What it is not | — (absence) | |
| Custodian by 3 absent ticks, reversible | Player states | 7 | |
| Custodian by concession, permanent | Player states | 7 | |
| Custodian defends, never expands or attacks | Player states | 7 | |
| Flagged "custodian since T" on every map | Player states | 7, 8 | |
| Garrisons weaken per absent tick | Player states | 7 | |
| Conquered-from-custodian yields half, forever | Player states | 7 | |
| Week-one custodian scores zero | Player states | 7 | |
| Exile and Gone unreachable in Stage A | Build order | 7 | |
| Capital guard 12 ticks, visible countdown | Pacing | 7, 8 | `ACapitalIsGuardedForTheFirstTwelveTicks`, `AGuardedCapitalCannotBeBesieged` (countdown is step 8) |
| Public score; leader always visible | Pacing | 7, 8 | |
| Fixed end tick; placement by score | Pacing; Scoring | 7 | |
| Dominance threshold held N consecutive ticks | Pacing | 7 | |
| Visibility rule; shared scouting lifts fog | Diplomacy UI | 8 | |
| Fleets public in transit once departed | Shape | 8 | |
| Sealed region placed and drawn; no rules | Build order | 2 | `TheSealedRegionIsPlacedAndReachable`, `AGalaxyWithoutARegionIsRefused` |
| Determinism: same seed + orders → same state | R16 | 0, 3, 4, 9 | `TheSameSeedGivesTheSameMatch`, `ResolvingTheSameTickTwiceGivesTheSameState`, `TenTicksOfNothingAreReproducible` |
| Galaxy is a pure function of rules and seed | R16 | 2 | `TheSameSeedGivesTheSameGalaxy`, `DifferentSeedsGiveDifferentGalaxies`, `PrngTests` |
| Presence is told to the sim, never a clock in it | R16 | 7 | |
| A tick log exists for *Replay tick N* to read | ADR-014 open q. | 4 | `ResolvingATickAdvancesItAndLogsSixPhases`, `TheCombatPhaseRunsAndSaysItDidNothing` |
