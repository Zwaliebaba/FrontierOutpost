# 4X-03 — Before Phase 0: what six people would hit first

**Status:** **Not started.** Written 2026-09-13 against `Design/blueprint.md` (§2, §3, §4, §7, §9 as
of commit `02f970c`) and the gap check of the same day, which read the blueprint's present-tense
claims against the tree and found four places where they disagree. **This plan is written to be
executed by Claude Code in build sessions**, one step per session or per commit, in the order
given. `Design/README.md` §5 governs a build session; `AGENTS.md` governs the code; §6 below says
how the two apply to this plan in particular.

`4X-01` built the loop and `4X-02` put it on a network; both are done except for the one thing no
code can do — six people on six machines. This plan is everything the tree still owes *before* that
Saturday: two bugs on the diplomacy path that nobody has hit because nobody has had two offers
open at once, one decided rule the resolver does not yet apply, the one rules change the owner
chose to land ahead of Phase 0 (building levels and build time, blueprint §3), a scripted scenario
§8 of the blueprint asks for, and one platform nuisance. It ends when the tree matches the
blueprint and Phase 0 can start on it.

---

## 0. Where the tree is, and why that shapes the order

**Everything in blueprint §3 is built and tested except what §3 says is not.** 467 tests across
five suites, all passing on 2026-09-13 (Debug|x64, `vstest.console.exe`). The resolver, the
snapshot boundary, persistence, the schedule, the log, reconnection, bots, the seats screen and
the digest-as-order-surface are all there. Nothing in this plan changes the platform: no wire kind
is added, no message shape beyond the fields named in Steps 2 and 3, no file beside the store and
the log (AGENTS.md R13).

**Four things the blueprint says that the tree does not do**, found on 2026-09-13 by reading the
code, not the record:

1. Concession forfeits score in any week (blueprint §3, owner decision 2026-09-11).
   `GameLogic/TickResolver.cpp:376-380` forfeits only when `tick < firstWeekTicks`. ADR-023's
   status line says the ADR and the code change are owed. **Step 1.**
2. A proposal card gets its ACCEPT / DECLINE buttons by matching a *proposal id* against a
   *player id*. `Lockstep/SnapshotView.cpp:519-527` compares `state.proposals[index].id` to
   `entry.other.Index()`; the resolver fills `.other` with the sender (`TickResolver.cpp:457`).
   The `|| state.proposals.size() == 1` fallback is why every test and rehearsal so far has
   passed. The digest entry has no proposal field at all (`GameLogic/TickLog.h:100-104`).
   **Step 2.**
3. The client answers one proposal per tick: `MatchState::orders.answeredProposal` is a single
   index (`LockstepClient/MatchState.h:356`), while `OrderSet::answers` is a vector and the
   resolver takes many. With six players and a four-tick window the second offer waits, and can
   expire as *ignored* — a false tell. **Step 2.**
4. `GameLogic/Snapshot.h:20` says the client greys a system whose last-seen stamp is old; nothing
   in the client reads `asOfTick`. The blueprint's own sentence — "not drawn yet" — is the true
   one. **Step 3 fixes the comment** while it is in that header; drawing the stamp is not in this
   plan.

**One rules change the owner chose to land before Phase 0.** Blueprint §3, *Building and
research*: three levels per building, each costing more and taking ticks to complete, a build in
progress visible to rivals who can see the system, and levels-plus-build-time built *before*
Phase 0 as the credit sink — because a starting empire builds out for 105 credits against a purse
of 100 and about 12 a tick, and credits are dead by day two. The bastion and the research track
are designed there too and are **not** in this plan; they wait for Phase 0's evidence on the combat
numbers. **Steps 3 and 4.**

**Two things the record asks for before real people arrive.** Blueprint §8: three-way fights favour
the largest, "it deserves a scripted scenario before real people find it" — **Step 5**, a test, no
rule. `4X-02` §7: `getaddrinfo` still blocks the frame loop on a hostname, "six people typing a
dotted address are unaffected; six people typing a name are not" — **Step 6**, last, and the one
step the owner may drop.

**Why this order.** Steps 1 and 2 are small, independent and on the path H1 measures; doing them
first means the levels work starts from a tree whose diplomacy path is right. Step 3 changes
`MatchRules`, the resolver, the snapshot and the scripted match's pinned hash, so it is one
session with nothing else in it. Step 4 is the screen for Step 3 and cannot start before it. Steps
5 and 6 touch nothing the others do.

---

## 1. What "done" looks like

The blueprint's §3 and the resolver agree on concession. Two open offers on one digest each carry
their own buttons, and both can be answered at one lock. A build is ordered at one lock, sits on
the rail with a tick-ETA like a fleet, is seen rising by a rival with a fleet next door, lands at
the lock its ETA named and produces that tick; a second build at the same system waits; a
mining station at level three pays what the table says. The bots level up when everything they
hold has a level one. The scripted match's hash is re-pinned and agrees between Debug and Release.
The three-way fight has a test with numbers in it that Phase 0 can compare against. A hostname in
the join box does not freeze the screen.

`Design/blueprint.md` §4 moves the levels rows from *designed, not built* to the built table; §9's
follow-ups for concession and levels are closed; `Design/UI/SCREENS.md` and `README.md` show the
build sheet and the rail as they now are, with captures retaken; `GETTING-STARTED.md` tells a new
player what a level costs. This plan moves to `Design/Archive/`.

Not in "done": the bastion, the research track, the *Propose* row in the build sheet, the sealed
region through fog, the last-seen stamp on the map, player names on the wire, twelve seats drawn,
screens 02, 07 and 08. All of those are listed in the blueprint with the phase they belong to.

---

## 2. Decisions already made (do not reopen these)

| Decision | Where | What it binds here |
|---|---|---|
| Concession forfeits score outright, in any week | blueprint §3, §9 (owner, 2026-09-11) | Step 1 implements it; the ADR records it |
| Three levels per building; cost rises; each level takes ticks | blueprint §3 (owner, 2026-09-13) | Step 3 |
| A build in progress is visible to rivals who can see the system — a tell | blueprint §3, §9 (owner, 2026-09-13) | Step 3's snapshot and digest |
| Levels and build time before Phase 0; bastion and research after | blueprint §3, §7, §9 (owner, 2026-09-13) | This plan's scope |
| Research is an empire order, not a building | blueprint §9 | Nothing here; do not add a building kind |
| The numbers live in `MatchRules`; no rules file | blueprint §3, §9 (owner, 2026-09-13) | Every new number is a `MatchRules` field with an initial value and a `Validate` check |
| The price is on the button; the client never knows a rule | ADR-053 | Level costs and times travel on the snapshot |
| A build is offered once, on the event that makes a player want it | ADR-057 | The offer logic offers the *next level* |
| A build sheet is about one system; a system you do not hold opens none | ADR-058 | Level rows live on that sheet |
| A rail row is a link to what it names | ADR-060 | A rising build's rail row opens its system's sheet |
| Every record is described once | ADR-049 | New snapshot and digest fields go through the archive, not a second reader |
| A match is a seed and its orders; the store carries the rules | ADR-024, ADR-042 | Level tables are part of the archived rules; no store migration is needed because no match is kept across this change |
| The executable ships alone; no dependencies | AGENTS.md R13, R14 | — |

---

## 3. What you must decide, and record as ADRs

Each is a decision with alternatives, so each is an ADR (`Design/README.md` §4), numbered next in
sequence — **067 is the next free number as this is written; take whatever is free when you get
there.** Write it `Proposed`, implement, and mark it `Accepted` in the same commit as the code that
implements it, which is how every ADR in this tree was landed.

1. **Concession forfeits score in any week** (Step 1). Supersedes the open question in ADR-023;
   amend ADR-023's status line to say so. The alternative to record honestly is the one the code
   has: first-week only, on the argument that a late concession is a courtesy to the attacker and
   should not be punished. The owner rejected it on 2026-09-11.
2. **A digest entry about a proposal names the proposal** (Step 2). Alternatives: infer it on the
   client from sender and lane (ambiguous the moment one player has two offers open, and a hold or
   a scouting offer has no lane); or send nothing and keep the `size() == 1` fallback (which is
   the bug). Also record here that the client holds one answer *per open proposal* rather than one
   per tick, and why the alternative — refuse the second tap — was rejected: the resolver takes
   many, and a player made to wait a tick is a player whose second offer expires as *ignored*.
3. **A building has three levels and takes ticks to rise** (Step 3). The decisions inside it that
   have alternatives, each to be weighed in the ADR:
   - *One construction per system at a time*, a second order refused with a named reason —
     against a per-system queue (more state, a second thing on the wire, and a queue is a floor
     plan by another name).
   - *Capture cancels a construction in progress*, credits gone, the loser told — against handing
     it to the new owner (a prize the siege did not earn) or refunding it (a siege that costs
     nothing).
   - *A custodian's paid construction still completes* — defending territory is not expanding it.
   - *A construction completes in phase 1 of the tick its ETA names and produces in phase 2 of
     that tick* — the same place a build lands today (`TickResolver.cpp:405-427`), so level one at
     one tick is exactly one lock later than today and nothing else moves.
   - *Cost and time tables travel on the snapshot* (six costs, six tick counts) — against a
     per-system "next price" (less on the wire, but the client could not show a level it cannot
     yet afford and the sheet is meant to say what level three costs).
   - *Level three is a number, not a feature, in this plan.* Blueprint §3's table says level
     three "opens a feature" and names two — a mining station doubling its lanes, a shipyard
     "repairing" a fleet. Ships have no damage state, so the second means nothing; the first is
     unlock-shaped and belongs with the bastion and the research track after Phase 0, which is
     for numbers. Record it, and amend the blueprint's table in the same commit to say that level
     three's features arrive with the bastion.
4. **A rising build is a row with an ETA** (Step 4). The representation: on the sheet, on the
   rail, in the digest, and *not on the map* in this plan — against a marker on the node, which
   is the natural next thing and is left to the UI record's list of what the map does not draw.
5. **A hostname resolves where the connect already waits** (Step 6). Names ADR-043 rather than
   amending it. Against `GetAddrInfoExW` with an overlapped completion: more Win32 surface for the
   same result on a thread that already exists.

None of these bends a rule in `AGENTS.md`. If one turns out to, say so in the ADR and the report.

---

## 4. The work, in order

#### Step 0 — Read, then check the tree is the one this plan describes

Read `AGENTS.md` in full, `Design/README.md` §4–§6, blueprint §2, §3, §4, §9, ADR-018 through
ADR-024, ADR-031, ADR-039, ADR-049, ADR-053, ADR-057, ADR-058, ADR-060 through ADR-065. Build
Debug|x64 and run the five suites: 467 tests as this is written. Open the eight places §0 cites and
confirm each still reads as described. **A line number that has moved is fine; a claim that is no
longer true is a finding — report it before touching anything.**

#### Step 1 — Concession forfeits score in any week

`TickResolver.cpp:376-380`: `forfeitedScore = true` on concession, unconditionally. The concede
row's second line (`Lockstep/SnapshotView.cpp:267`, "Hand this empire to a custodian. Permanent.")
says the cost: *Permanent, and your score is forfeit.* — the armed row (ADR-064) is where a player
reads it before the second tap, and eight-pixel copy has to fit the row.

**Tests (`GameLogicTests/PlayerTests`):** `ConcedingInWeekThreeForfeitsScore` beside the existing
first-week case; the existing `ACustodianAfterTheFirstWeekKeepsTheirScore` stays true because
absence is not concession. `LockstepTests/SignalTests`: the concede row's detail names the cost.
If any scripted policy concedes, the pinned hash changes — it should not; say so either way.

**Record:** the ADR (§3.1); ADR-023's status line; blueprint §3 ("the code today forfeits only a
first-week concession" goes), §4 (the *Concession* row leaves the not-built table), §9 (follow-up
closed).

#### Step 2 — A proposal card knows its proposal, and every open offer can be answered

**Wire.** `DigestEntry` gains `ProposalId proposal` (`TickLog.h`, beside `system`, `lane`,
`fleet`, `other`), archived with the rest (ADR-049). Every `Tell` about a proposal sets it:
`ProposalReceived`, `ProposalAnswered`, `ProposalWithdrawn`, `ProposalIgnored`, `ProposalVoided`
(`TickResolver.cpp`, grep `DigestKind::Proposal`). `.other` keeps meaning the counterparty.

**Client.** `SnapshotView.cpp:519-527` matches `state.proposals[index].id` against
`entry.proposal.Index()` and the `size() == 1` fallback goes. `MatchState::orders` holds a
`std::vector<ProposalAnswer>` — `{ proposal index, accepted }` — in place of `answeredProposal` /
`acceptedProposal`; `OrdersOf` emits one `AnswerOrder` per entry. A card whose proposal is
answered shows which way (`ACCEPTED` / `DECLINED` in the button's place, tap again to change), and
the rail's PROPOSALS row says the same. Edit counting (ADR-031) is per envelope and does not change.

**Tests.** `GameLogicTests/DiplomacyTests`: `AProposalDigestEntryNamesItsProposal`, for every one
of the five kinds. `LockstepTests/DigestViewTests`: two offers from two rivals on one digest each
carry their own ACCEPT and DECLINE, aimed at the right proposal; two offers from the *same* rival
likewise. `LockstepTests/TapTests`: accept one, decline the other, and the `OrderSet` carries two
answers with the right ids; tap ACCEPT twice and it is one answer, not two. The scripted match:
the diplomat bot's offers are answered by `BotPolicy`, not by this path, so the hash should not
move; confirm.

**Record:** the ADR (§3.2); blueprint §2 already describes the card correctly and needs no change;
`Design/UI/SCREENS.md` 01 gains the answered state of a proposal card.

#### Step 3 — Levels and build time: the rules

One session, nothing else in it. It touches `MatchRules`, `Match`, `TickResolver`, `Snapshot`,
`TickLog`, `BotPolicy` and the scripted match's pinned hash.

**`MatchRules`.** `BUILDING_LEVELS = 3`. The four scalars become per-level tables with these
initial values — Phase 0's to change, so nothing derives from anything else:

| Field | Level 1 | Level 2 | Level 3 | Replaces |
|---|---|---|---|---|
| `shipyardCost` | 20 | 40 | 60 | `shipyardCost` |
| `miningStationCost` | 15 | 30 | 50 | `miningStationCost` |
| `shipyardBuildTicks` | 1 | 2 | 3 | — |
| `miningStationBuildTicks` | 1 | 2 | 3 | — |
| `shipsPerShipyard` | 2 | 3 | 4 | `shipsPerShipyard` |
| `miningStationCredits` | 4 | 7 | 10 | `miningStationCredits` |

`Validate` refuses a cost table that does not strictly rise, an effect table that falls, and a
build time of zero — a level that costs no more than the one before is a mechanic that has ceased
to exist, which is the standard the existing check applies to the lane. The rules X-macro in
`MatchSimulation.cpp` must archive every element, described once; add the array form rather than
listing eighteen scalars. `PhaseZeroRules()` and `PracticeRules()` leave the tables alone: build
times are not fractions of a match.

**`SystemState`** (`Match.h:21-43`): `shipyardLevel` and `miningStationLevel` (0 = none) replace
the two bools everywhere they are read; a `Construction { kind, toLevel, completesAt }` with
`completesAt == 0` meaning none. One per system.

**Orders.** `BuildOrder` keeps its shape — system and kind — and means *the next level of that
kind at that system*. `Match::Validate`: `AlreadyBuilt` becomes `AtTopLevel`; a new
`AlreadyBuilding` refuses a second order while one is in progress; `CannotAfford` prices the next
level; the running-purse rule is unchanged. Every new reason is phrased for the digest with the
numbers, as the existing ones are (ADR-053).

**Resolution.** Phase 1: pay the level's cost, set the construction with
`completesAt = tick + ticks[level]`, narrate *P1 started Mining station L2 at Jandal, done T47*.
Also in phase 1, before anything else: every construction whose `completesAt` is this tick
completes — level up, construction cleared, narrated — so it produces in phase 2 of this tick.
Phase 2 reads the tables by level; a shipyard rising to level two still produces at level one
while it rises. Phase 5: a system that changes hands loses its construction (§3.3). A custodian's
construction completes.

**Digest.** Four kinds, each with its severity placed per ADR-020: `BuildStarted` (own),
`BuildCompleted` (own), `BuildLost` (own, on capture — a loss), `BuildSeen` (a rival's, *P3 raises
a shipyard at Jandal - done T47*, sent once at the start to every player for whom the system is
live that tick, never repeated — one digest per tick, not one per event).

**Snapshot.** `SnapshotSystem` carries both levels and, for a live system, the construction;
a remembered system carries the levels as last seen and **no construction** — the same rule as
sieges (`Snapshot.h`). The two prices become the six costs and six tick counts, and the client
reads numbers from them and nothing else. Fix the comment at `Snapshot.h:20` while there: the
stamp travels and is not drawn.

**Bots** (`BotPolicy.cpp:181-205`): the cheapest affordable next level across held systems,
skipping systems under construction; the turtle still takes shipyards first. Every policy must
still produce a full 84-tick match without an order rejected for a reason a bot could have known.

**Tests (`GameLogicTests`).** `ABuildOrderedAtTLandsAtTPlusItsTicks`;
`ALevelOneBuildProducesTheTickItCompletes` (and not the tick before);
`ASecondOrderOnASystemUnderConstructionIsRefused`; `ATopLevelBuildingIsRefused`;
`EachLevelCostsMoreAndPaysMore`; `RulesWithADescendingLevelAreRefused`;
`CaptureCancelsAConstructionAndTellsTheLoser`; `ACustodiansPaidBuildStillCompletes`;
`ARivalWhoSeesTheSystemSeesItRising`; `ARivalWhoCannotSeeTheSystemLearnsNothing` (the snapshot's
negative test, extended — this is the security boundary, ADR-022);
`ARememberedSystemReportsLevelsAndNoConstruction`; `TheBotsLevelUpOnceEverythingHasALevelOne`;
`PhaseZeroRulesLeaveBuildTimesAlone`. The scripted match's final hash **will** change: re-pin it
from a Debug run, then build `GameLogicTests` at Release|x64 and run it, because CI's Release job
is the gate that catches a hash the two toolchains disagree on (`AGENTS.md` §6).

**Record:** the ADR (§3.3); blueprint §3 (the table's level-three column, per §3.3), §4 (levels
and build time move to the built table; the row for the bastion and research stays), §9
(follow-up closed); `MatchRules.h`'s comments cite the ADR.

#### Step 4 — Levels and build time: the screen

**`BuildRow`** (`MatchState.h:258-275`) gains the level it would build, the ticks it takes, and —
for a system under construction — the ETA. `SnapshotView.cpp:450-500` composes, per held system:
one row per kind below level three and not under construction, *Shipyard L2 - Jandal* / *+3 ships
a tick - 2 ticks* / `40 CR`; a system under construction gets one row that is not a target, *Mining
station L2 rising - done T47*. The offer-once logic (ADR-057) offers the next level.

**The build sheet** (ADR-058): one row per kind — *SHIPYARD L1 > L2 - 40 CR - 2 TICKS*, *MINING
STATION L3 - MAX* when there is nowhere to go, *RISING - DONE T47* while it rises — with the price
on the button (ADR-053) and the row dim when the purse cannot cover it.

**The rail.** BUILDS keeps its `QUEUED -40` rows and gains in-flight rows for every construction
on a held system, *MINING L2 JANDAL - T47*, in the form FLEETS uses for a fleet under way; a row
links to its system's sheet (ADR-060). The header's `N AVAIL - X CR` counts rows that can be
queued.

**The digest.** The four kinds of Step 3 get cards: started and completed in the economy colour;
lost with the loss colour and a loss's rank; a rival's rising build in the economy colour with the
rival as its actor, so actor grouping (ADR-034) folds it under that rival.

**Not on the map.** A rising build is not drawn on the node in this plan; the UI record's list of
what the map does not draw gains the line.

**Tests (`LockstepTests`).** `DigestViewTests`: each of the four cards, and a rival's card grouped
under its actor. `TapTests`: queue a level two from the sheet and the rail shows `QUEUED -40` with
the running total; a system under construction shows its rising row and offers nothing; a top-level
building offers nothing. The existing build tests keep passing with level one where they said
"shipyard".

**Captures.** Retake `01-build-sheet.png`, `01-orders-queued.png`, `01-main-page.png` and
`01-rail-hover.png` per `Design/UI/README.md` *Photographing the build*; add `01-build-rising.png`
(a practice match one tick after queuing a level two). Update `SCREENS.md` 01 and the README's
screen table and its "what the design asks for" list.

**Record:** the ADR (§3.4); `GETTING-STARTED.md` §1's building paragraph (levels, and that a build
takes ticks); blueprint §2's sheet sentence gains the level rows.

#### Step 5 — The three-way fight, pinned

`GameLogicTests/CombatTests`: `ThreeWayFightsFavourTheLargest` — 30 ships against 15 and 15 at
one system under the current numbers, the exact survivors asserted and written into the test's
comment with the arithmetic; and `AHoldFireAgreementDoesNotStopTheTwoSmallSidesShootingEachOther`,
because a hold is recorded and not enforced (blueprint §3) and a Phase 0 player will assume
otherwise. No rule changes; no ADR. The numbers in the test are what Phase 0 compares its
battles against.

**Record:** blueprint §8's three-way paragraph says the scenario exists and names the test.

#### Step 6 — A hostname resolves where the connect already waits

ADR-043 took the connect off the frame loop; `getaddrinfo` still runs on it. Move resolution into
the same attempt, so the join screen's dialog says `RESOLVING <host>` and then `CONNECTING`, and an
unknown name lands on the join screen with the 05 dialog's refusal in the form the others take.
`PointerInput` and the frame loop are not touched.

**Tests (`NeuronClientTests` or `NeuronCoreTests`, wherever `MatchConnection`'s tests live):** a
name that cannot resolve reports its refusal without the loop having waited on it — measure the
frame loop's cadence during the attempt, since "did not block" is a number, not a feeling
(`Design/README.md` §3).

**Record:** the ADR (§3.5); `4X-02` §7's item 3 struck through with the date; `SCREENS.md` 05
gains the `RESOLVING` state.

This step is last because it is the one the owner may drop: a Phase 0 host who hands out a dotted
address never hits it.

#### Step 7 — Close out

Blueprint §Status (the test count, measured), §4, §9 and §10 agree with the tree. `Design/UI/`
agrees with the screen. `GETTING-STARTED.md` agrees with both. Every ADR this plan owed is
`Accepted`. This plan's status line says what was done, what was found, and what was left, and
the file moves to `Design/Archive/` with the header the archived documents carry. `4X-02` §7's
"What Phase 0 now needs" is updated: item 1 — two machines, one match, one tick — is unchanged and
is the whole of what remains.

---

## 5. Things that will tempt you, and the answer

- **Keeping `hasShipyard` beside `shipyardLevel` "for compatibility".** No. One representation;
  the store keeps no match across this change (ADR-042's stores are per match and Phase 0 has not
  started), so there is nothing to be compatible with.
- **Putting the level tables in the client so the sheet can say "L3" before the snapshot arrives.**
  No (ADR-053). The client owns the sentence; the server owns the number.
- **Zero build time at level one, to keep today's feel.** No. The ETA is the point of the change:
  nothing a player builds is in flight today, and a level one that lands at once is a level one
  with no ETA. One tick is one lock later than today, and that is the whole of what moves.
- **A build queue per system.** No. One construction at a time, and a second order is refused
  with a reason the digest can print. A queue is a floor plan by another name.
- **Drawing the rising build on the map while you are in `MapRender`.** Not in this plan; it goes
  on the UI record's list. `AGENTS.md` §6: stay in scope.
- **The bastion, the research row, the *Propose* row — "while I was in there".** No. Each is
  designed and each has its phase. This plan is the list, and the list is closed.
- **Adjusting a number so the bots' scripted match "looks better".** No. Every number is Phase 0's
  to change, with people. Re-pin the hash the numbers give you.
- **Skipping the Release build of `GameLogicTests` after re-pinning.** No. The hash is pinned across
  toolchains on purpose (`AGENTS.md` §6), and CI will say what you did not.
- **Recording the level decisions in a commit message instead of an ADR.** No
  (`Design/README.md` §5). If you are writing a paragraph of justification, it was a decision.

---

## 6. How to run this plan

- **One step per session, one commit per step at least**, in the order of §4. Do not start Step 4
  before Step 3's suites are green and its hash is re-pinned in both configurations.
- **Every session starts with Step 0's reading**, however far along the plan is. Then the step's
  ADR, `Proposed`; then the code and the tests; then the record; then `Accepted`, all in one
  commit with an imperative subject (`AGENTS.md` §6).
- **Build and test the way CI does** (`AGENTS.md` §3): `msbuild Lockstep.slnx /p:Configuration=Debug
  /p:Platform=x64 /m /v:minimal /nologo`, then `vstest.console.exe` over the five DLLs. MSBuild and
  vstest are PowerShell tools; find `vstest.console.exe` under the Visual Studio install if it is
  not on the path. For Step 3, also `/p:Configuration=Release /t:GameLogicTests` and run that
  suite.
- **Run the three checkers before every commit**: `Build\CheckFormat.py`, `Build\CheckProjectFiles.py`,
  and `Build\RunClangTidy.py` from a Developer PowerShell (dot-source `Launch-VsDevShell.ps1`
  first). If `python` is not on the path, the launcher `py` is. Local clang-tidy and CI's are
  different releases; green locally is necessary, not sufficient — check CI after pushing.
- **Documents in `Design/` keep each file's own line ending.** The blueprint and the ADRs are
  CRLF; `GETTING-STARTED.md` is LF. Match the file you are in.
- **Captures** (Step 4) follow `Design/UI/README.md` *Photographing the build*, and cannot be taken
  on a locked desktop — check before starting.
- **Report per `Design/README.md` §6**: what was verified, what was assumed, which rule was bent,
  what was noticed and left alone. "Builds clean, not run" and "builds and runs" are different
  claims.

---

## 7. The rule checklist

Every rule this plan owes, with where it lands. A row without a test at close-out is a rule that
was dropped, and the report says so.

| Rule | Where it is decided | Step | Test (to be named at close-out) |
|---|---|---|---|
| Concession forfeits score in any week | blueprint §3, §9 | 1 | `ConcedingInWeekThreeForfeitsScore` |
| The concede row says what it costs | ADR-064 | 1 | `SignalTests` |
| A digest entry about a proposal names it, all five kinds | §3.2 | 2 | `AProposalDigestEntryNamesItsProposal` |
| Two open offers each carry their own buttons | blueprint §2 | 2 | `DigestViewTests` |
| Every open offer can be answered at one lock | blueprint §3 | 2 | `TapTests` |
| Three levels per building; cost rises; effect does not fall | blueprint §3 | 3 | `EachLevelCostsMoreAndPaysMore`, `RulesWithADescendingLevelAreRefused` |
| A build lands in phase 1 of the tick its ETA names and produces that tick | §3.3 | 3 | `ABuildOrderedAtTLandsAtTPlusItsTicks`, `ALevelOneBuildProducesTheTickItCompletes` |
| One construction per system | §3.3 | 3 | `ASecondOrderOnASystemUnderConstructionIsRefused` |
| Top level refused with a reason | §3.3 | 3 | `ATopLevelBuildingIsRefused` |
| Capture cancels a construction; a custodian's completes | §3.3 | 3 | `CaptureCancelsAConstructionAndTellsTheLoser`, `ACustodiansPaidBuildStillCompletes` |
| Rising build visible to those who see the system, and to no one else | blueprint §3, §9; ADR-022 | 3 | `ARivalWhoSeesTheSystemSeesItRising`, `ARivalWhoCannotSeeTheSystemLearnsNothing` |
| A remembered system reports levels, never a construction | ADR-022 | 3 | `ARememberedSystemReportsLevelsAndNoConstruction` |
| Prices and times on the wire, never in the client | ADR-053 | 3, 4 | `SnapshotTests`, `TapTests` |
| Phase 0 and practice presets leave build times alone | §3.3 | 3 | `PhaseZeroRulesLeaveBuildTimesAlone` |
| Bots level up | §4 Step 3 | 3 | `TheBotsLevelUpOnceEverythingHasALevelOne` |
| Determinism: hash re-pinned, Debug and Release agree | AGENTS.md R16, §6 | 3 | `ScriptedMatchTests` |
| Sheet rows per level with price and ticks; a rising row that is not a target | ADR-053, ADR-058 | 4 | `TapTests` |
| Rail ETA rows link to the sheet | ADR-060 | 4 | `TapTests` |
| Four build cards, a rival's grouped under its actor | ADR-020, ADR-034 | 4 | `DigestViewTests` |
| Three-way fight pinned; a hold does not stop the small sides shooting each other | blueprint §8, §3 | 5 | `ThreeWayFightsFavourTheLargest`, `AHoldFireAgreementDoesNotStopTheTwoSmallSidesShootingEachOther` |
| A hostname does not block the frame loop | 4X-02 §7 | 6 | to be named |

---

### What Phase 0 still needs from a person

Unchanged from `4X-02` §7: **two machines, one match, one tick.** Everything in this plan is
verified on loopback and in the suites. When this plan is done, that is the only item left.
