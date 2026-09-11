# 4X-02 — The server, the client and the network

**Status:** Plan, not started. Written properly 2026-09-11, when `4X-01` closed Stage A and the
shapes this plan is specified against became facts. Written against `space-4x-one-pager-v10.md`
(v0.7) and `space-4x-prototype-test-plan.md` (v0.4).

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
neutral while a `Snapshot` carries standings for six to twelve. Step 2 is where both are fixed, and
widening `Owner` is a **design question about the token palette**, not a mechanical one.

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

The first is an **owner decision and blocks Step 1**. The rest are the session's, recorded in the
step that meets them — the same discipline `4X-01` used, and it worked: five ADRs written against
code that existed rather than one written against five guesses.

**ADR — Persistence, and what R13 means for a server.** *(Owner decision.)* A three-week match must
survive the server process. R13 says the executable ships alone with no runtime file dependency, and
it was written for assets. The options:

- **(a)** The server writes one file beside itself; R13 is read as binding the *client*, which still
  ships alone. The rule gains a sentence saying so.
- **(b)** Memory only. A restart loses the match. Survivable for Phase 0's forty-eight hours and for
  nothing after it.
- **(c)** A database. R14 forbids it — the build depends on the Windows SDK and the MSVC standard
  library and nothing else.

*Recommendation:* **(a)**, with the file being the seed plus every locked order set rather than a
snapshot — which makes it the same decision as the next one. **Whichever way this goes, AGENTS.md R13
changes in the same commit**, because a rule that the tree visibly breaks is a rule nobody will
believe the next time.

**ADR — Persist and replay by re-resolving.** Loading a match is re-running it from tick zero. The
file is a seed, a `MatchRules`, and the locked order sets in tick order; `4X-01`'s measurement says
that costs about two milliseconds for a whole match. It buys a tiny file, gives *Replay tick N* for
free, and turns a determinism bug into a load that visibly diverges instead of a corruption that
does not. It costs one real thing: **the resolver can never change an in-flight match's history
without a migration**, so a rule fix mid-Phase-0 either ends the match or accepts that the replay no
longer reproduces it. This is the payoff ADR-018 exists to make possible.

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

**`MatchState::Owner` widens here**, and it is the design question named in §0: twelve
distinguishable colours at 8px, against a token palette that already spends its hues on meaning
(loss, contact, the region). Expect this to need an owner decision and a change to
`Design/Screens/README.md`, not just a bigger enum.

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

The server logs every event the test plan lists, timestamped UTC.

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
