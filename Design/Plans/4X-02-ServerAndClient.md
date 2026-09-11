# 4X-02 — The server, the client and the network

**Status:** Plan, not started. Written properly 2026-09-11, when `4X-01` closed Stage A and the
shapes this plan is specified against became facts. **Both owner decisions are taken** (2026-09-11):
persistence is ADR-024, and `MatchState::Owner` widens to twelve authored colours, so nothing blocks
Step 1. Written against `space-4x-one-pager-v10.md` (v0.7) and
`space-4x-prototype-test-plan.md` (v0.4).

`4X-01` built the loop headless: a galaxy, a resolver, every rule in the one-pager's build order,
and six scripted policies playing a full 84-tick match deterministically in one process. **This plan
puts that behind a server, in front of the main page, and on a network**, so the test plan's Phase 0
can run — six people, a one-hour tick, a forty-eight-hour match.

It ends when six humans on six machines finish a match and the server has the log to prove what
happened.

---

## 0. What `4X-01` left you, and what it did not

**The simulation is done and is a value.** `Frontier::Match` is copyable, hashable and holds no
pointers into itself. `TickResolver::Resolve(match, TickInput, TickLog&) -> Match` is a pure
function: state and orders in, the next state and a log out. There is no clock in it, no global,
and no randomness that is not derived from the match seed (ADR-018). You do not need to understand
the rules to write this plan's code; you need to call one function at the right moment.

**Three shapes are settled and are the ones you will send.**

| | What it is | Where |
|---|---|---|
| `OrderSet` | One player's decisions for one tick. Ids, enums, counts and flags; **no free text**, asserted by `NoOrderCarriesFreeText` | `GameLogic/Orders.h` |
| `Snapshot` | What one player is entitled to know, at one tick, plus their combat previews | `GameLogic/Snapshot.h` |
| `TickLog` | What happened in each of the six phases, and one digest per player | `GameLogic/TickLog.h` |

All three already serialise, through `Neuron::ByteWriter` / `ByteReader`: little-endian by explicit
shifting, a read past the end sets a flag rather than being undefined, and a declared count past a
sanity bound is refused rather than reserved. That reader was written for a socket and has never
seen one. **Its error paths are the part of it that is untested against reality**, and Step 3 is
where that changes.

**`TickInput` already has the field you need.** It carries orders *and* `present` — who the server
saw since the last lock — because they are different facts and only a server can observe the
second. `presenceUnknown` defaults to true, which is what `4X-01`'s callers meant; **your server
always sets it false and fills `present`.** Getting this wrong makes every player a custodian on
tick three, or nobody ever.

**Replaying a whole match costs 2.2 ms** in Release, 86.6 ms in Debug
(`Design/Reference/tick-resolution-cost.md`). That figure is what makes §2's persistence
recommendation viable, and it is measured rather than hoped.

**What is not built, and is not this plan's either:** Exile, Gone, the sealed region's rules,
raiding, salvage, the runway, hiring, accounts, matchmaking, a mobile client, chat. `PlayerStatus`
declares Exile and Gone and a test asserts they stay unreachable. If this plan makes one reachable,
that is a finding, not a feature.

**One thing `4X-01` left half-done on purpose.** The client still renders `MatchFixture` —
the design reference's hand-typed tick 46 — and `MatchState::Owner` still has three players and a
neutral while a `Snapshot` carries standings for six to twelve. Step 2 is where both are fixed. The
owner has chosen **twelve authored colours** (§3, Step 2); authoring them is still a design session
against the token palette rather than a constant table.

---

## 1. What "done" looks like

Six people on six machines play a forty-eight-hour match at a one-hour tick, from a client that
draws real state, and the server's log answers every question the test plan's Phase 0 asks — when
each player logged in, what they ordered, what resolved, and what they saw.

A server restarted mid-match resumes it, and says so: the replayed hash matches the one it last
wrote. A client reconnecting gets the current snapshot without the server having kept anything
about that client.

Not in "done": more than one match per server process, matchmaking, accounts that outlive a match,
or anything about ranking between matches.

---

## 2. What you must decide, and record as ADRs

**Both owner decisions are already taken** and are recorded below with what they rule out. The rest
are the session's, written in the step that meets them — the same discipline `4X-01` used, and it
worked: six ADRs written against code that existed rather than one written against six guesses.

**~~ADR — Persistence, and what R13 means for a server.~~ Decided: ADR-024, owner decision,
2026-09-11.** This no longer blocks Step 1. The server writes **one match store** — the rules, the
seed, and every locked order set — and a match is loaded by **re-resolving it from tick zero**, with
the replayed hash asserted against the last one written. AGENTS.md R13 was amended in the same
commit and now binds the shipped client, naming this store as its one exception.

Two things ADR-024 decided that this plan has to honour and not quietly soften:

- **The hash check at load is not optional and not debug-only.** It is the whole safety argument for
  storing orders rather than state.
- **The resolver can never change an in-flight match's history without a migration.** A rule fix
  during Phase 0 either ends the running match or accepts that its replay no longer reproduces it.
  There is no third option.

**ADR — The seam.** ADR-007's `Simulation` was `ApplyOrder(MoveToOrder) / Tick() / Snapshot()` and is
Deprecated. The 4X needs an order *set* per player in, and a per-player snapshot and digest out,
because players see different things (ADR-022). ADR-007's *argument* still holds and should be
re-used rather than re-derived: the dependency graph forbids `NeuronServer → GameLogic`, so the
interface lives in `NeuronCore` and the test suite gets a counting fake.

**ADR — Time and the schedule.** The interval and the phase of day are match parameters
(`MatchRules::tickIntervalSeconds`). A `TickSchedule` is pure arithmetic over an injected clock, so
tests drive it and the simulation still never sees one (R16). **Decide what happens to a lock the
server slept through** — the recommendation is to resolve each missed tick in order with the orders
it has, because a match that skips a tick has a hole in its order list and stops replaying.

**ADR — Transport.** ADR-006's queues were 20 Hz in one process and are Deprecated. This game is
four messages a day per player and cares about reliability rather than latency, so TCP with
length-prefixed frames is the obvious answer and the ADR mostly exists to say why nothing cleverer
is needed. `NeuronCore.h` already includes WinSock2 and links `ws2_32`.

**ADR — Identity for Phase 0.** A per-match, per-player token, issued by whoever starts the server
and typed once into the client. The ADR should be honest that this is not authentication and says
what it would take to become it. **Log every login**: the login curve is the test plan's primary
instrument, and a Phase 0 that cannot draw it answers nothing.

---

## 3. The work, in order

Three stages. **B** is the server around the loop, **C** wires the client to it in-process, **D**
puts it on a network. Nothing in any of them changes a rule; if one wants to, that is a finding for
the owner and it goes back to `4X-01`.

### Stage B — the server shell

#### Step 1 — The seam, the session, the schedule, the file

`NeuronCore/Simulation.h` returns with the 4X signature from the seam ADR. `NeuronServer/Session`
owns one, accepts order sets from identified players, marks presence between locks, and at each lock
resolves, persists, and makes the new snapshots and digests available.

Persistence is the seed, the rules and the ordered locked order sets. Loading is re-resolving, and
**the session asserts the replayed hash against the last one it wrote** — that assertion is the
whole reason this design is safe, so it is not optional and not debug-only.

A lock the server slept through is resolved on wake, once per missed tick, in order.

**Tests (`NeuronServerTests`, which still holds only a `SuiteSmoke` and loses it here):**
`TickSchedule` arithmetic against fixed instants, at a one-hour and a six-hour interval and across a
day boundary; a `CountingSimulation` proving the session resolves once per lock and never between;
a session saved, discarded and loaded producing the same hash; two missed locks producing two
resolutions in order; presence marked by a connection *between* locks and not by one after.

#### Step 2 — The fixture retires

`FrontierOutpost` decodes a `Snapshot` into `MatchState`, sends the orders rail's edits as an
`OrderSet`, takes the countdown from the server's next lock instant rather than a local number, and
points *Replay tick N* at the server's `TickLog` instead of the stub view.

**In-process first** — session and client in the same executable, as MVP-01 did — so this stage is
verifiable before any network exists. `MatchFixture` becomes a test fixture and stops being the boot
path, which is what its header always said would happen. `GeneratedMatch` goes with it.

**`MatchState::Owner` widens here, to twelve authored colours** — owner decision, 2026-09-11,
choosing that over two alternatives that were offered and rejected:

- *Four semantic colours* (you, ally, rival, neutral) reads instantly and never runs out, and was
  rejected because it cannot tell two rivals apart on the map. The one-pager's late game is "how to
  read other humans", and a map that renders every rival identically deletes it.
- *A smaller palette plus a per-node glyph* was rejected as crowding the map at twelve players.

So this step owes **an ADR authoring the twelve colours**, and it is a real design session rather
than a constant table: the token palette already spends hues on meaning — loss, contact, the region,
the trade lane — and twelve owner colours have to be distinguishable from those and from each other
at 8px on black. `Design/Screens/README.md`'s token list changes with it. Do it against the running
client, not on paper.

**Tests (`NeuronClientTests`):** snapshot to `MatchState` against a hand-built snapshot; an edited
orders rail serialising to the `OrderSet` the server expects; a fogged snapshot producing a map with
holes in it rather than a crash. And run it — all seven interactions from ADR-014 still working
against real state.

### Stage C — the network

#### Step 3 — Six machines

The transport and identity ADRs, implemented. TCP, length-prefixed frames, the server listens, each
client holds one connection and a token.

**This is where `ByteReader`'s error paths meet reality for the first time.** They were written for
this and have only ever been fed bytes this tree wrote. Fuzz them: truncated frames, a length field
larger than the frame, a frame larger than any legitimate message, and a client that disconnects
mid-message. A reader that is merely *not undefined* is not the same as a server that stays up.

A `--serve` flag or a second executable; the choice belongs to the transport ADR. Note that a second
executable changes AGENTS.md §2's "nine projects" and that **R13 binds the client** either way.

**The server logs every event the test plan lists, timestamped UTC.** That sentence used to be the
whole specification and it was not enough: the list is short, specific, and three of its entries are
awkward in ways that are only obvious once you try to emit them. It is written out here so nobody
has to rediscover which ones.

| Event | Where it comes from |
|---|---|
| login | The server. The **primary instrument** — the login curve is what Phase 1's H2 is measured on |
| session start / end | The server. A session is a connection, so "end" includes a disconnect nobody asked for |
| order edit | **The client**, and only the client. Edits happen before the lock and the server never sees the ones that were replaced. H4 needs it (≥ 80% of sessions include an order edit), so the client has to report it |
| order lock | The session, at each lock |
| message | **Does not exist.** v1 has no free text, and `NoOrderCarriesFreeText` asserts it structurally. Log nothing and say so, rather than leaving a reader wondering what happened to it |
| proposal sent / accepted / declined | `TickLog` digests: `ProposalReceived`, `ProposalAnswered`, `ProposalWithdrawn`, `ProposalIgnored`, `ProposalVoided` |
| trade lane opened / cancelled | `TickLog` digests: `LaneOpened`, `LaneCanceled` — and the **two cancel reasons are distinguished in the detail text**, which is the whole point of them being distinguished |
| capital fall | `TickLog` digest `SystemLost` on a system whose `kind` is `Capital` |
| custodian takeover | `TickLog` digest `Custodian`, and `PlayerState::custodianSince` |
| **fleet order after capital fall** | **Nothing emits this today.** It is H3's entire measurement — whether losers keep playing — and it is a *join*: a `FleetOrder` in a locked `OrderSet` from a player whose capital fell on some earlier tick. The session has both halves and is the only thing that does |

Two more the test plan asks for outside that list:

- **Defender dancing** (Phase 0's watch item) is already emitted by the simulation as
  `TickLog::interceptions`, with `Dodges()` as the numerator. The server logs the pair per tick.
  Note that the watch item has **two** clauses — dodged more than a third of the time it was
  targeted, *and* the dodging player retains or retakes the system. Only the first is in the
  `TickLog`; the second spans ticks and is the reader's join.
- **H5, custodian neutrality**, needs the winner's adjacency to custodian-run empires across
  several matches. That is analysis over finished match stores, not a live event.

**Tests:** framing round-trip; the fuzz cases above; a client reconnecting after a server restart
getting the current snapshot; a token not on the match's list refused. And the real one: two
machines, one match, one tick.

#### Step 4 — Close out

Move both plans to `Design/Archive/` with a *What shipped* section above each, as MVP-01 has. Update
AGENTS.md §2's repository map — `GameLogic` and `NeuronServer` are no longer empty and their rows
should say what they hold. Confirm every §2 ADR exists. Report per `Design/README.md` §6, including
**what Phase 0 needs that this plan did not build**.

---

## 4. Things that will tempt you, and the answer

**"The client could compute the combat preview itself."** It cannot: the client does not link
`GameLogic` (AGENTS.md §2), and the preview is already computed server-side into the snapshot by the
same `ResolveMelee` the battle runs. A second implementation is a preview that will eventually
disagree with the fight.

**"The snapshot has nearly everything; let it have the rest."** Every field in a `Snapshot` gets
sent. `Snapshot.cpp` says so at the top and `TheSnapshotContainsNothingThePlayerIsNotEntitledTo` is
the test that catches it. A convenience field the client "needs anyway" is a field a player can read
off the wire.

**"Resolve on demand when a client asks."** The tick is the heartbeat. Resolution happens at the
lock, once, for everybody — a client that could provoke one would be a client that could see the
future half a tick early.

**"Phase 0 needs accounts."** It needs a token and a login log. Everything else is v2.

**"The server should validate orders itself."** `Match::Validate` already exists and is the only
implementation. Call it; do not write a second one that agrees with it today.

---

## 5. Open questions this plan leaves to the session

**Whether the match should stop resolving when it ends.** `Match::IsFinished()` is reported and
nothing enforces it — the resolver will happily run tick 85. The server is the right place to decide,
and ADR-023 says so without saying what it should do.

**What a reconnecting client is owed.** The current snapshot, certainly. The digests it missed,
probably — the one-pager's whole premise is that you open the app and read what changed since you
last looked, and a player who was away for three ticks has three digests waiting.

**Whether one server process holds one match or several.** Phase 0 needs one. Building for several
before anyone has run one is the mistake this plan's predecessor avoided by being a stub.

**Whether `MatchRules`' defaults survive the test plan's compressed phases.** They are authored for
a 21-day match at four ticks a day and every phase is shorter: Phase 0 is 48 ticks at a one-hour
tick, Phase 1 is 56 at six hours. Two defaults do not scale with that on their own —
`firstWeekTicks` is 28, which is well over half a Phase 0 match, and `regionOpensAtTick` is 60,
which never arrives in either. The region has no rules until Phase 2 so the second is harmless for
now; the first means "a first-week custodian scores nothing" covers most of a Phase 0 run. Setting
them per phase is a one-line change and belongs to whoever configures the match, but somebody has
to notice, so it is written down here.
