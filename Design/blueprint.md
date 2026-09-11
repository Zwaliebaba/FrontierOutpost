# Frontier Outpost — Blueprint

**What this is.** The one document to hand to another team — engineering, art, community, ops, or a
game designer you want to recruit — that says what *Frontier Outpost* is, why it is worth making,
what exists today, and where it goes next. It is written from the design record in `Design/` and
from the code as it stands on **2026-09-11**, and it keeps the record's discipline: what is built is
described in the present tense, what is designed and not built says so, and what is undecided is
listed as a question rather than papered over.

**In one sentence.** Frontier Outpost is an asynchronous, tick-quantised space strategy game for six
to twelve humans, played four times a day for three weeks, where every order is a blind bet placed
against rivals placing theirs — and where losing your capital is the start of the best story the
game can tell, not the end of yours.

**Status, honestly.** The whole core loop is built, deterministic, tested (333 automated tests across
four suites) and running end to end over TCP: galaxy generation, the six-phase tick, combat, sieges,
trade lanes, proposals, custodianship, fog of war, scoring, the fixed ending, a match store that
survives restarts, and the instrumentation the playtest plan needs. What has not happened is the
thing no code can do: six people on six machines playing a match. Exile and the sealed region's rules
are designed and deliberately not built, gated behind the first playtests.

---

## 1. The pitch

### The moment

You open the app after work. Your scout has arrived at the system you were expanding toward, and a
rival's outpost is already there. Their fleet is one tick out from yours. Your orders lock at the
next tick, theirs do too, and neither of you will see what the other chose until it resolves.

That is the whole game in one paragraph. Everything else is machinery to make that moment happen
several times a day, for three weeks, between people who have never met.

### Why it is not another 4X

Most strategy games ask you to spend hours. Frontier Outpost asks for thirty minutes, twice a day,
and makes those thirty minutes count. The nearest relatives are *Neptune's Pride* and *Subterfuge*
— slow, social, betrayal-shaped — and the differences are deliberate:

- **The core verb is *commit*.** Nothing happens between ticks. Orders are hidden until they lock and
  public once they have moved. You never react in real time; you read, you decide, you leave
  something in flight, you come back to find out. The tension is in the gap.
- **Combat is deterministic.** No dice. The client shows you exactly what a fight will do from what
  you can see. Uncertainty comes from one source only: what the other humans ordered.
- **Diplomacy without chat.** There is no free text in the game at all. Strangers click but don't
  write, so every diplomatic act is a button — propose a trade lane, share scouting, hold fire — that
  locks with your other orders and lands in the counterparty's next digest. Agreement moves at the
  speed of betrayal, and both are visible.
- **Losing has a second act.** When your capital falls you are not eliminated. You become an *Exile*:
  a fleet and a colony core with no territory, raiding and salvaging your way toward a sealed region
  of the galaxy that everybody can see and everybody knows will open on a fixed tick. Settle there
  and you are back in the game as a small, late empire. A settled exile who tops the score on the
  final day wins outright. *(Designed; not yet built — see §6.)*
- **A defined ending.** Every match ends on a date known when it starts. Placement, not victory,
  earns season points: in a match of eight, seven players lose, and fourth has to be worth playing
  for.

### Who it is for

People who like reading a situation more than clicking fast. People who will happily hold a grudge
across a working week. People who want a strategy game that fits around a life rather than replacing
one. The game's own words for its aesthetic target are **Challenge** first and **Discovery** second,
with **Narrative** arising as a by-product: the story of my empire this match, including — if it
falls — the story of its exile.

---

## 2. What you see

One screen. The design calls it the **ops console**, and it is deliberately a console rather than a
cinematic: 1280×720, a single 8×8 bitmap font, terse copy, no anti-aliasing, drawn on black. Three
columns:

**The digest, left — the primary read.** One list per tick, never one notification per event, sorted
by consequence: a capital lost ranks above a battle, which ranks above first contact, above a
proposal, above the economy line. It is the thing you read first and the thing the whole session is
organised around. Tap an event and the map focuses on it.

**The map, centre — commitments as overlays.** The galaxy is a graph: systems are nodes, lanes are
edges, and every lane carries an integer tick cost. It is drawn on a tilted ground plane under a real
perspective orbit camera that you drag to look around, with a procedural starry sky behind it.
Systems rise on stems above their shadows; fleets in transit hover above their lane with a tick-ETA;
trade lanes, proposed lanes, sieges, custodians and the sealed region are all drawn as what they are.
Fog is remembered: a system you once saw stays on your map, greyed, stamped with the tick you last
saw it.

**The orders, right — three columns that lock together.** Fleets, builds, proposals. A fleet card
shows where it is going and a combat preview ("14 v 11 (+def) · 6 left") computed by the server from
what you can see. The build list holds shipyards and mining stations — and the **trade lane**, which
is a building with two owners, sitting in the build menu with *Propose* where *Build* would be.
Diplomacy has no tab. An open proposal is a card with ACCEPT and DECLINE, and answering it is itself
an order that locks with the rest.

The top bar carries the lock countdown, your score and placement, and the leader — always visible,
because leader-ganging is the game's only anti-snowball.

---

## 3. How a match plays

| | |
|---|---|
| **Players** | 6–8 for the prototype, up to 12 by design. No AI fills seats. |
| **Galaxy** | Generated per match from a seed, sized to the player count: 31 systems and 45 lanes for six players, 61 and 90 for twelve. A ring: every capital has two neighbours at the same distance, a starting cluster of two satellites on one-tick lanes, and a rival capital within three ticks. The frontier and the sealed region sit in the middle, equidistant from everyone. |
| **Cadence** | Four ticks a day at fixed UTC times in production. The interval is a match parameter: playtests run an hourly tick. |
| **Length** | Three weeks (84 ticks). Ends on the date it said it would, or earlier if one player holds 60% of all score for four consecutive ticks. |
| **A session** | Thirty minutes: read the digest, adjust orders, answer a proposal, leave something in flight. A longer session plans several ticks ahead and studies rivals. Same loop, different depth. |

**The loop is scout → claim → build → contest.** Starting clusters are dense so first contact happens
on day one; lane costs grow toward the frontier so late events span several ticks. The galaxy is
meant to be fully claimed by roughly day five, after which the only way income grows is trade lanes
with neighbours — and the build menu keeps saying so.

### What resolves at the tick

Six phases, in a fixed order, each reading the state at the start of the tick and writing the next,
so that processing order can never change an outcome:

1. **Lock.** Every player's orders are applied; every open proposal is re-validated against the new
   state and voided with a reason if it no longer makes sense.
2. **Production.** Systems, capitals, mining stations and lanes pay out. A trade lane pays each owner
   more than any internal lane — that gap is the entire incentive to talk to a neighbour.
3. **Movement.** Every fleet advances. Fleets pass each other on lanes; combat only happens at
   systems. *Movement before combat means leaving beats arriving:* a fleet ordered out the same tick
   a hostile arrives escapes, at the price of ceding the system for a tick.
4. **Combat.** One melee at every system holding two or more empires' fleets. Simultaneous rounds,
   damage spread across enemies in proportion to their strength, integer arithmetic, three rounds.
   The incumbent gets a defender bonus; simultaneous arrivals at an empty system get none; a tie is
   mutual attrition. A rear-guard sub-phase that punishes dodging exists and is switched off until
   playtests show dodging dominates.
5. **Claims and captures.** An uncontested fleet claims an unclaimed system. Taking an owned system
   needs two consecutive uncontested ticks: siege, then capture. A fleet that arrives and dies
   contests nothing. Trade lanes that lose an endpoint cancel, and the digest says *cancelled by
   partner* or *cancelled: system lost* — never just *cancelled*, because the tell only works if you
   know which.
6. **Digest.** One per player, sorted by consequence.

### The three decisions the game is about

**Where to expand.** Near and safe, far and rich, or toward a rival to deny them. Each is right in a
different situation.

**Where the fleet is.** Ships can defend or push, never both, and lanes make every allocation a
visible commitment others can read and exploit.

**Whom to trust.** No enforced treaties. The trade lane is the one consensual mechanic: both must
agree, either can cancel at any tick, and lanes are public. Proposals stay open four ticks so every
player sees them in at least one daily session; an unanswered one is reported to the proposer as
*ignored*, which is itself a tell. Share-scouting and hold-fire agreements are recorded and *not*
enforced — a breach is reported to both sides, and the sanction is entirely social.

### What a player learns

Early: how to read the map — which lanes matter, which systems are worth the reach. Mid: tempo —
claiming one tick earlier beats claiming one system more; chokepoints decide wars before fleets do.
Late: how to read other humans — who over-extends, who bluffs, who is quietly winning.

### Pacing devices

Capitals cannot be attacked for the first twelve ticks, with a visible countdown. Score is public and
recomputed every tick from what you hold *now*, so a leader can be dethroned on the last tick and
ganging up on them always works. The sealed region is visible from tick one and opens mid-match at a
known tick — a race, not a reward.

### When a player stops playing

Four states. **Active** is normal play. **Custodian** is what territory becomes when its player is
absent for three ticks (reversible — log in and resume) or concedes (permanent): it defends, never
expands, never attacks, is flagged on every map as "custodian since tick N", and its garrisons weaken
each tick so it is a public race among every neighbour rather than a private farm. Systems taken from
a custodian yield half for the rest of the match, whoever holds them. A player who goes custodian in
the first week scores nothing for the match — the only cost that reaches someone who has already
left. **Exile** and **Gone** are the second act described in §1 and specified in §6; both are
declared in the code and deliberately unreachable today.

### What it is not

Not a chat game. Not real time. Not persistent — nothing carries between matches but rank. Not a
coordinate map — there is no velocity to tune. Not a base builder — buildings are unlocks and
production, not floor plans. Not a sandbox — the map runs out and the match ends.

---

## 4. What exists today

This section is for the teams who will pick the project up. It is an inventory, not a promise.

### The game, in code

| Built and tested | Where |
|---|---|
| Galaxy generator: ring layout, constraint validation, seed rejection, 64 embedded system names | `GameLogic/GalaxyGenerator` |
| `MatchRules`: every tunable in one struct with initial values, plus a check that refuses rules under which a mechanic ceases to exist | `GameLogic/MatchRules.h` |
| The six-phase resolver, every rule in §3, with a per-tick log for *Replay tick N* | `GameLogic/TickResolver`, `Melee` |
| Orders: fleet moves, builds, proposals with conditional lanes, answers, withdrawals, lane cancellation, concession — eighteen distinct rejection reasons, each phrased for the digest | `GameLogic/Orders`, `Match::Validate` |
| Per-player snapshot as the security boundary: a player receives exactly what they are entitled to see, and a test asserts the negative | `GameLogic/Snapshot` |
| Six scripted policies (expand-near, expand-far, turtle, raider, diplomat, absentee) playing a full 84-tick match, hash-identical across runs | `Tests/GameLogicTests/ScriptedMatchTests` |

### The platform around it

| Built and tested | Where |
|---|---|
| **One executable, three roles**: host-and-play (default), `--join <host>`, `--serve` (headless). The client talks TCP even to a server on the next thread, so every launch exercises the wire | `FrontierOutpost.cpp`, ADR-028 |
| The server drives the game through a byte-shaped seam and never names a game type | `NeuronCore/Simulation.h`, `NeuronServer/Session`, ADR-025 |
| **A match is a seed and its orders.** The store holds rules, seed and every locked order set; loading is re-resolving from tick zero with the replayed hash asserted against the last one written. A whole match replays in about 2 ms in Release | `NeuronServer/MatchStore`, ADR-024 |
| The schedule is arithmetic over an injected instant; a server that slept through locks resolves every one it missed, in order | `NeuronCore/TickSchedule`, ADR-026 |
| Length-prefixed frames, six message kinds, every length checked at three layers, fuzzed with truncated and hostile input; socket tests run on real loopback sockets | `NeuronCore/{FrameStream,Protocol,Socket}`, `NeuronServer/MatchServer` |
| Six fixed seat tokens for Phase 0 — a stable identity to log, explicitly not authentication | ADR-029 |
| Instrumentation to a UTC-stamped plain-text file: every event the test plan lists, including the awkward ones (fleet orders after a capital fall, order edits counted as envelopes) | `NeuronServer/MatchLog`, ADR-030, ADR-031 |
| Client reconnection, a `RECONNECTING` indicator, `--phase0` rules and `--tick <seconds>` for compressed rehearsals | `FrontierOutpost/MatchConnection` |
| The ops console: immediate-mode UI, perspective orbit camera, spherical star field with a galactic band, twelve owner colours with *you* always blue | `FrontierOutpost/MainPage`, ADR-014, -017, -027, -032, -033 |

A rehearsal at two seconds a tick has run a full 48-tick Phase 0 match over sockets with two clients
and four absentees, and found two instrumentation bugs that reading the code had not. That is the
closest thing to a played match so far.

### Engineering shape, for the teams that will touch it

C++23, Direct3D 12, Windows 11, x64, MSBuild. **No dependencies** beyond the Windows SDK and the MSVC
standard library — no engine, no package manager, no vendored library. **The executable ships alone**:
font, colours, shaders and system names are embedded; the only files a server ever creates are the
match store and the log. The simulation is integer-only, reads no clock, iterates no unordered
container, and is a pure function of its inputs — which is what makes replays, persistence and
"why did my fleet die" all answerable. The client never links the game logic; the server is
authoritative and the snapshot is the whole contract between them. Anyone building a second client
(see §7) needs to speak the snapshot and the order set, and nothing else.

### Designed, not built

| Not built | Why not yet |
|---|---|
| Exile: the runway, salvage, raiding, raid fatigue, the colony core, settlement | Gated behind Phase 1's hypothesis H3 — *do losers keep playing?* |
| The sealed region's rules | It is placed, drawn and reachable; nothing happens when it opens. Phase 2. |
| Hiring exiles (escrowed jobs) | v2 |
| Research | Named in the phase list, deliberately absent until something reads it |
| Accounts, matchmaking, a lobby, player names (players are "P2" on screen) | Phase 1 needs at least per-match tokens and names |
| Season ranking between matches | Nothing carries between matches today |
| Missed digests on reconnect; a mobile or web client; chat | See §7 and §8 |

---

## 5. How it will be validated

Three bets are stacked — the tick loop, Exile, the sealed region — and each depends on the one
below. The test plan tests bottom-up and gates each layer on evidence, not enthusiasm.

**Phase 0 — mechanical shakeout.** Six friends, a one-hour tick, a 48-hour match. Find broken
mechanics; tune the capital guard, lane yields, combat numbers, the dominance threshold. Watch for
defender dancing and switch the rear-guard on if it dominates. Explicitly *not* evidence about
retention, silence or session shape — a compressed clock hides all of it. **This is the next step,
and the tree is ready for it.**

**Phase 1 — the loop with strangers.** Six to eight strangers, a six-hour tick, fourteen days, three
matches minimum. Five hypotheses with pass and kill thresholds:

| | Hypothesis | Kill signal |
|---|---|---|
| H1 | Strangers engage in diplomacy | < 25% send any proposal by day 5 → redesign diplomacy before anything else |
| H2 | The tick and length hold attention | < 40% daily logins by day 7 → cadence or length is wrong |
| H3 | Losers keep playing | < 25% of fallen players issue fleet orders afterward → Exile becomes a one-screen epilogue |
| H4 | The 30-minute session exists | Median > 60 min or < 5 min → fix digest and order UX |
| H5 | Custodian territory is neutral | Winners consistently border custodians → tighten it |

**Phase 2 — Exile and the region.** Only after Phase 1 gates. Six to eight strangers, three weeks.

The login curve is the primary instrument. Every event needed to draw it is already written to the
log by the code as it stands.

---

## 6. The second act: Exile (design intent)

Because it is the part most likely to make a designer lean in, here is the full intent, all of it
unbuilt.

When your capital falls you carry out a **runway** of salvage sized so the sealed region is reachable
by the shortest lane path from anywhere on the map without raiding once it is open. Upkeep draws the
runway down each tick; a fleet that isn't fed shrinks. You extend it by salvaging battles and
**raiding** mining stations and trade routes — raiding is how you arrive stronger, not how you
arrive, and **raid fatigue** makes repeated hits on the same target diminish so sustained spite
starves you. Early exiles must survive until the region opens; late exiles just race. Only a colony
core can settle a site in the region, and there are several. Empires can raid the region but never
claim it.

Exiles score like everyone else, on what they hold at match end. Salvage scores nothing. The ticks
spent holding nothing are the penalty, proportional to how early you fell. There is no cap: a settled
exile who tops the score wins. The one exclusion is that a settled exile cannot trigger the early
dominance ending. In v2, empires can **hire** an exile for a job with payment held in escrow — the
one enforced transaction in the game.

Absence as an exile for three ticks, or a fleet reaching zero, is **Gone**: the fleet dissolves into
salvage where it stands, for whoever is near enough.

---

## 7. Where the game goes next

A view, not a plan. Each step depends on the previous one holding.

**Now → Phase 0 (weeks).** Six people, one weekend. The build is ready; what it needs is a host, six
tokens and a Saturday. Expect to change numbers, not rules.

**Phase 1 (one to two months).** The smallest things that make strangers possible: tokens generated
per match and printed by the host, player names on the wire, the digests a reconnecting player
missed, and a way to fill a match. Three matches before any gate decision.

**Phase 2 — the second act (a quarter).** Exile and the region, as designed in §6, if H3 says losers
keep playing. If it says they don't, Exile becomes an epilogue screen and the region a plain
mid-match objective — that is a scoped-down game, not a failed one.

**Seasons and identity (v2).** Accounts, placement points across matches, a ladder that makes fourth
place worth playing for, hiring exiles. This is where "nothing carries between matches but rank"
starts to mean something, and where matchmaking becomes the product's real cold-start problem.

**A second client.** The one-pager's opening image — *you open the app after work* — is a phone in a
pocket, and the tree is a Windows desktop window. The architecture was built for this move: a client
is anything that can read a snapshot and write an order set over TCP, the server is authoritative,
and the game logic has no floating point to disagree with another compiler about.
`Design/Reference/mobile-portability.md` costs the move honestly. The renderer, the window shell and
the fixed 1280×720 contract do not travel; the simulation, the protocol and the input core do. A
portrait, digest-first mobile client — with the digest delivered as a push notification — is the
version of this game most people would actually play, and it is a new client, not a port.

**What the architecture makes cheap, and nobody has asked for yet.** Because a match is a seed and a
list of orders, a finished match is a few kilobytes that replays in two milliseconds. That is a
shareable replay, a spectator mode, a "story of my empire" export, a balance-analysis corpus and a
regression suite for every rule change, all from one file format that already exists. The same
property makes a compressed **evening match** — two-minute ticks, one sitting — a product mode rather
than a test tool; it is already how the loop is rehearsed.

**Design threads worth pulling later.** What research unlocks. Whether "strength" stays as ship
count or grows ship classes. A small vocabulary of one-tap signals that are not chat but let a
stranger say *thank you* or *last warning*. A colour-blind mode, which at twelve hues on an 8-pixel
node is a redesign of the node and not of the palette. Whether the sealed region should be visible
through fog from tick one, as the design says and the code does not yet do.

---

## 8. Risks, and where I would push back

The one-pager names four risks and they are the right four: **silence** (strangers may never engage
in diplomacy, and then trust is not a decision and the game is a solitaire race), **the Exile
hypothesis** (a player whose capital falls may simply leave), **the median player** (most people in
every match lose), and **cold start** (six to eight strangers committing to three weeks is a
matchmaking problem before it is a design problem). The test plan is built to kill each of them
early. Beyond those, reading the record and the code as a designer, these are the places I would
stress-test before Phase 1:

**Absence is punished harder than a life allows.** Custodianship after three missed ticks is eighteen
hours at the production cadence, and a custodian inside the first week scores zero for the whole
match with no way back. A weekend away in week one ends a three-week commitment. The compressed
rehearsal already tripped over exactly this and had to scale the number by the clock rather than the
match. The mechanic is right; the number and the permanence of the forfeit look wrong for the
audience the game wants, and H2 will be measured on people this rule has already pushed out.

**The last tick is a blind, simultaneous, everything-on-the-table lock.** Score is what you hold at
the end, and the end tick is public. Every player's final orders resolve together with no reply
possible. That is either the most dramatic moment in the game or an invitation to hold everything
back for one coordinated snipe at T83. It should be watched deliberately in Phase 0 rather than
discovered in Phase 1.

**Three-way fights favour the largest.** Proportional damage means two smaller empires meeting the
leader at a contested system shoot each other as much as the leader. That works against leader-ganging,
which is the only anti-snowball the game has. ADR-021 flags it; it deserves a scripted scenario
before real people find it.

**Three rounds may not resolve a big fight.** Two large equal fleets end a tick attrited and still
facing each other, which starts a siege neither can finish. Interdiction, or a stalemate generator —
only play will say.

**The premise and the platform disagree.** The design is a phone game; the build is a Windows-only
desktop client with no web or mobile path. For a game whose hardest problem is filling a match with
strangers, reach is not a nice-to-have. Phase 0 and 1 can run on desktop; the product cannot.

**No chat means the community lives elsewhere.** The no-free-text rule is a strong, defensible design
position, and it also means every conversation about the game happens on Discord or nowhere. That is
a community-team question the record does not address.

---

## 9. Open questions for the owner

The record leaves these undecided, or does not mention them, and a team reading this blueprint will
ask. None of them blocks Phase 0.

1. **The name.** The one-pager is titled *Untitled Space 4X*; the tree, the window and the screen say
   *Frontier Outpost*. Is Frontier Outpost the name, or a working title?
2. **Fiction and setting.** There is no lore anywhere in the record — no factions, no why-are-we-here,
   no tone beyond "terse ops console" and a table of sixty-four system names. Marketing, art and
   audio all need a sentence about what world this is. Is the absence a decision?
3. **The shipping platform.** Desktop Windows for real, or desktop as the prototype and mobile as the
   product? The answer decides the client roadmap in §7 and the resolution baseline in the design
   record.
4. **Business model.** Nothing in the record. Free, paid, season pass, cosmetics? It shapes the season
   design more than any rule does.
5. **Season structure.** How many matches make a season, how placement points accumulate, whether
   rank gates matchmaking. "Placement, not wins" is a principle without a table yet.
6. **What research does**, and whether ship classes ever exist. Both are named and empty.
7. **Whether concession forfeits score.** The one-pager says it does under Exile; ADR-023 leaves it
   open for a custodian who conceded. Same word, two rules.
8. **Whether the first-week forfeit should be permanent**, and whether absence should be measured in
   real time rather than ticks (§8).
9. **Audio.** Nothing exists and nothing is designed. Silence may be right for an ops console; it
   should be a decision.
10. **Accessibility.** Colour-blindness is named as unaddressed in ADR-027. Is it in scope for v1?

---

## 10. Where to read more

| Want | Read |
|---|---|
| The rules, as designed | `Design/space-4x-one-pager-v10.md` |
| How the playtests decide what gets built | `Design/space-4x-prototype-test-plan.md` |
| Every decision and what it rejected | `Design/ADR/` — ADR-018 through ADR-031 are the game; 014, 017, 027, 032, 033 are the screen |
| What was built, step by step, with what it found | `Design/Plans/4X-01-CoreLoop.md`, `Design/Plans/4X-02-ServerAndClient.md` |
| The screen, at reference fidelity | `Design/Screens/README.md` |
| How code is written here | `AGENTS.md` |
| What a phone client would cost | `Design/Reference/mobile-portability.md` |
