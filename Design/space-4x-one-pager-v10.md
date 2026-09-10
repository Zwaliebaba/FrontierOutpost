# Untitled Space 4X — One-Pager v10

## Aesthetic target
**Challenge** (primary) · **Discovery** (secondary). Narrative arises as a by-product: "the story of my empire this match" — including, if it falls, the story of its exile.

**The moment:** you open the app after work. Your scout has arrived at the system you were expanding toward, and a rival's outpost is already there. Their fleet is one tick out from yours. Your orders lock at the next tick, theirs do too, and neither of you will see what the other chose until it resolves.

## Nearest existing games
Neptune's Pride, Subterfuge. What is different here: Exile, a sealed region everyone can see and count down to, and a fixed three-week season with placement scoring.

## Shape of a game
- One bounded galaxy sized to the number of humans — 6–8 for the prototype, up to 12 by design. No AI fills seats.
- The galaxy is a graph: systems are nodes, lanes are edges, and every lane carries a tick cost assigned at generation. Distance is authored, not emergent. The generator guarantees each capital a rival capital within three ticks, one-tick lanes inside starting clusters, and two- to four-tick lanes toward the frontier; a seed that can't satisfy this is rejected.
- Asynchronous and tick-quantised. Four ticks a day at fixed UTC times. Orders can be edited at any time and lock at the tick; nothing else happens between ticks. Combat is deterministic: uncertainty comes from what humans ordered, not from dice, and the client shows a preview of any engagement from visible information.
- Orders are hidden until they lock. Once a fleet departs it is visible in transit with its tick-ETA. Commitment is blind at the moment of choice and public afterwards.
- A match lasts three weeks and ends on a date known when it starts.
- A 30-minute session: read the tick digest, adjust orders, answer a proposal, leave something in flight. A multi-hour session: plan several ticks ahead, negotiate, study rivals. Same loop, different depth.

## Tick resolution
Every phase reads the state at the start of the tick and writes to the next; nothing reads what another order wrote in the same tick, so processing order can never change an outcome.
1. **Lock** orders and proposals.
2. **Production** and research.
3. **Movement.** Every fleet advances. Fleets pass each other on lanes; combat happens only at systems.
4. **Combat**, in two strict sub-phases. **4a, rear-guard** (contingent, see below): fleets that departed a system this tick while a hostile arrived there take one free round from the arrivals, computed from the arrivals' end-of-movement strength. **4b, system combat**, read from the post-4a state, at every system holding hostile fleets: one melee, each fleet's damage spread across enemies in proportion to strength, fixed rounds, integer arithmetic. An incumbent fleet gets the defender bonus; simultaneous arrivals at an empty system get none. A tie is mutual attrition, not a coin flip. Phases are sequential by design — production feeds movement, 4a feeds 4b; the no-read rule is about orders within a phase, not phases.
5. **Claims and captures**, evaluated on the survivor snapshot after combat. A claim requires presence uncontested by any surviving hostile fleet at the end of the tick; two surviving hostiles leave a system occupied and unclaimed. Capturing an owned system requires hostile presence uncontested by surviving defenders at the end of two consecutive ticks — siege, then capture. A fleet that arrives and dies contests nothing.
6. **Digest.**

Movement before combat means a fleet ordered out the same tick a hostile arrives escapes. Leaving beats arriving: the defender's bet is whether to stay. This makes hunting a fleet that refuses to fight impossible by design — interdiction still works, because the fleet holding a chokepoint is the incumbent — but every dodge cedes the system for a tick and starts a siege the dodger must return into. Sub-phase 4a is switched off until Phase 0 shows dancing dominates; if enabled, dancing stays possible and stops being free.

## Core loop
**Scout → claim → build → contest.**
The core verb is *commit*: every order is a bet placed now and resolved later, against rivals placing theirs in the same window with the same blindness.

**Design rule:** no session may end without something in flight. There is always a pending event with a visible tick-ETA. The player receives one digest per tick, never one notification per event — the cadence is twice a day, not hourly. The digest is the primary screen: what changed since you last looked, sorted by consequence. The map is second, with commitments — fleets in transit, open lanes, pending proposals — drawn as overlays.

## The three most interesting decisions
1. **Where to expand.** Near and safe, far and rich, or toward a rival to deny them. Each option is right in a different situation and wrong in the others.
2. **Where the fleet is.** Ships can defend or push, never both; lanes make every allocation a visible commitment that others can read and exploit.
3. **Whom to trust.** No enforced treaties. The one consensual mechanic is the **trade lane**: two neighbours who both agree earn more from a lane between them than from any internal lane, and either can cancel it at any tick. Lanes are public; cancelling one is a tell. Diplomacy is structured — **proposals** as buttons (open a lane, share scouting, hold for N ticks) — because strangers click but don't write. A proposal is an order: it locks with your others, arrives in the counterparty's next digest, stays open for four ticks so every player sees it in at least one daily session, can be withdrawn by the proposer while open, and takes effect at the first lock after acceptance. It can carry a conditional order — "if accepted, open lane" — so the effect lands without a second round trip. Every open proposal is re-validated at every lock against current state (endpoints still owned by the two parties, still adjacent); one that fails is voided and both digests say why, so nobody is ever shown a dead offer as acceptable. A lane accepted at a lock opens in that same phase 1 and pays from that tick's production. A lane that loses an endpoint in phase 5 cancels automatically, and the digest distinguishes *cancelled by partner* from *cancelled: system lost* — the tell only works if the reader knows which. Agreement moves at the same speed as betrayal.

## What the player learns over time
- **Early:** how to read the map — which lanes matter, which systems are worth the reach.
- **Mid:** tempo — claiming one tick earlier beats claiming one system more; chokepoints decide wars before fleets do.
- **Late:** how to read other humans — who over-extends, who bluffs, who is quietly winning.

## Pacing devices
- **Dense start, sparse frontier.** Starting clusters are tight so first contact happens on day one; lane costs grow toward the frontier so late events span several ticks. The tick stays constant.
- **Capital guard.** Capitals cannot be attacked for the first twelve ticks. This is an explicit rule with a visible countdown, layered on top of the siege rule, not derived from it. Everything else is takeable from tick one.
- **The galaxy is fully claimed by roughly day five** — a target set by galaxy size per player, not a rule. If playtests show it feels rushed, the map widens; the tick doesn't.
- **The sealed region.** Generated at map creation, visible from tick one, opens at a known tick mid-match. Everyone can see it and position for it. It is a race, not a reward.
- **Public score.** The leader is always visible. Leader-ganging is the anti-snowball; no mechanical rubber-banding beyond distance and supply cost.
- **A defined ending.** Fixed end date; placement by score. An early dominance threshold ends the match only if held for several consecutive ticks, so the leader stays attackable.

## Player states
Four states, six transitions. Nothing else exists.
- **Active.** Normal play.
- **Custodian.** Territory defends, never expands, never attacks. Entered by three ticks of absence (reversible — log in and resume) or by concession (permanent). Conceding never denies an attacker their prize. Custodians are flagged on every player's map as "custodian since tick N" and their garrisons weaken with each tick of absence, so the territory is a public race among every neighbour who can reach it, not a private farm. Systems conquered from a custodian yield at half for the rest of the match, whoever holds them — the dropout's infrastructure decays under new ownership. A player who goes custodian in the first week scores nothing for the match; it is the only cost that reaches someone who has already stopped playing.
- **Exile.** Fleet plus colony core, no territory, no construction — only repair. Entered from Active or Custodian when the capital falls, or by choice. Settling a site returns the player to Active as a small late empire.
- **Gone.** Fleet reaches zero, or an exile is absent three ticks — the fleet dissolves into salvage where it stands, for whoever is near enough to scavenge.

## Defeat: Exile
- On exile the player carries out a **runway** of salvage from the fallen capital, sized so that the sealed region is reachable by the shortest lane path from anywhere on the map without raiding once it is open. Upkeep draws down the runway every tick; a fleet that isn't fed shrinks.
- Exiles extend the runway and grow the fleet by salvaging battles and **raiding** mining stations and trade routes. Raiding is how you arrive stronger, not how you arrive. **Raid fatigue:** salvage from the same target diminishes with each raid, so sustained spite starves the exile.
- Early exiles must survive until the region opens, so they raid; late exiles just race. Both are intended.
- Only a colony core can settle a site in the region, and the region has several. Empires can raid it, not claim it.
- **Scoring:** exiles score like everyone else, on what they hold at match end. Salvage scores nothing; concession forfeits the empire's score outright. The ticks spent holding no territory are the penalty, proportional to how early you fell. There is no cap: a settled exile who tops the score on the end date wins, and it is the best story the game can produce. The one exclusion is on the win condition, not the score — a settled exile cannot trigger the early dominance threshold.
- *v2:* exiles can be **hired** by an empire for a job, with payment held in escrow on outcome — the one enforced transaction in the game.

## Scoring between matches
Season points by placement, not by wins. In a match of eight, seven players lose; fourth place has to be worth playing for. First-week custodians score nothing.

## Diplomacy UI
Diplomacy has no tab. A trade lane is a building with two owners, so it lives in the build menu next to shipyards and mining stations — "Trade lane with [neighbour]: +X/tick" — greyed out, with *Propose* where *Build* would be. The income screen shows lane income foregone the way it shows idle shipyards; after day five, lanes are the only way income grows, and the menu keeps saying so. The orders screen has three columns — fleets, builds, proposals — that lock together. First contact raises the game's own prompt: "Contact: [player]. Propose trade lane?" — one tap, no text. Shared scouting pays visibly, as fog lifting on the map. Pending proposals appear in the digest as events with countdowns, in the same form as an ETA. A proposal that runs its full four ticks unanswered is reported to the proposer as *ignored* — a tell, and a mild cost for silence. No free text in v1; strangers won't write and the design must not need them to.

## What it is not
Not a chat game — v1 has no free text.
Not real time. Not persistent — nothing carries between matches but rank. Not a coordinate map — there is no velocity to tune. Not layout design — buildings are unlocks and production, not floor plans. Not a sandbox — the map runs out and the match ends.

## Biggest known risks
1. **Silence.** Decision three assumes strangers will engage in diplomacy. If trade lanes and proposals don't get them talking, trust never becomes a decision and the game is a solitaire race. Test before anything else.
2. **The Exile hypothesis.** Exile assumes a player whose capital falls wants to keep playing. If they leave anyway, Exile is a graceful exit, not a mode, and should be scoped as one.
3. **The median player.** Most participants in every match lose. Placement scoring has to make the slow middle worth staying for, or dropout and silence follow from the win rate alone.
4. **Cold start.** Six to eight strangers committing to three weeks is a matchmaking problem before it is a design problem.

## Build order
Loop first — graph generator, tick resolution, trade lanes and proposals in the build menu, custodian, capital guard, siege rule, digest, fixed end. No Exile, no region. The exile runway is tuned in a spreadsheet before Phase 2 builds it. Defender dancing is a Phase 0 watch item with sub-phase 4a as its pre-built, switched-off fix. See the prototype test plan.
