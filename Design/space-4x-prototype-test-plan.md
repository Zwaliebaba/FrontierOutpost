# Prototype Test Plan — Space 4X v0.4

Three bets are stacked: the tick loop, Exile, the sealed region. Exile depends on the region; both depend on the loop. Test bottom-up. Nothing above a layer is built until that layer holds with strangers.

## Instrumentation (all phases)
Timestamped events for: login, session start/end, order edit, order lock, message, proposal sent/accepted/declined, trade lane opened/cancelled, capital fall, custodian takeover, fleet order after capital fall. The login curve is the primary instrument; surveys are secondary.

## Phase 0 — Mechanical shakeout
**Setup:** 6 players, friends, compressed clock: 1-hour tick, 48-hour match. Loop only.
**Purpose:** find broken mechanics, tune capital guard length, trade lane yield, lane costs, dominance threshold. Run as many as needed.
**Watch item — defender dancing:** log every departure that coincides with a hostile arrival at the same system. If a fleet escapes this way more than a third of the time it is targeted, and the dodging player retains or retakes the system, enable the rear-guard round and re-run.
**Explicitly not evidence for:** anything about async experience, silence, retention or session shape. Compression hides all of it.

## Phase 1 — The loop with strangers
**Setup:** 6–8 strangers, real cadence: 6-hour tick, 14-day match (shortened from 21 for iteration speed). Loop only. Losing a capital leaves the player with their fleet and nothing else — no upkeep, no region, no goal. That crude state is the Exile hypothesis test.

| # | Hypothesis | Pass | Kill |
|---|---|---|---|
| H1 | Strangers engage in diplomacy | ≥ 50% of players send ≥ 1 proposal or message by day 3; trade lanes open between ≥ half of neighbour pairs | < 25% engage in any diplomacy by day 5 → decision three does not exist with strangers; redesign diplomacy before touching Exile |
| H2 | The tick and match length hold attention | ≥ 60% of players log in daily through day 10; median 2 logins/day | < 40% daily by day 7 → cadence or length is wrong; Exile is irrelevant until fixed |
| H3 | Losers keep playing | Of players whose capital falls, ≥ 50% issue fleet orders in ≥ 3 subsequent ticks | < 25% → Exile is a graceful exit, not a mode; scope it to a one-screen epilogue |
| H4 | The 30-minute session exists | Median session 10–40 min; ≥ 80% of sessions include an order edit | Median > 60 min or < 5 min → the loop is either an obsession trap or a notification-checker; fix digest and order UX |
| H5 | Custodian is neutral | Winner's adjacency to custodian-run empires no higher than chance across matches | Winners consistently border custodians → custodian is free real estate; tighten defence or shrink it over time |

H5 needs several matches to mean anything; treat it as a watch item, not a gate, until n ≥ 5.

## Gates
- H1 and H2 both pass → proceed to Phase 2.
- H3 passes → build Exile as designed. H3 fails → build the epilogue version and drop the region-as-exile-goal; the sealed region becomes a plain mid-match objective.
- H1 fails → stop. Everything downstream assumes strangers talk.

## Phase 2 — Exile and the region
**Setup:** 6–8 strangers, 6-hour tick, 21-day match. Adds sealed region, colony core, upkeep and salvage, raid fatigue, concede-to-exile, rank ceiling. Hiring stays out.
**Hypotheses:** exiles who attempt settlement ≥ 50% of exiles; ≥ 1 settled exile per match; no empire consistently positioned to dominate the region at opening; raid targets not concentrated on one empire (spite check); concede-to-exile used by ≥ 1 player per match without becoming the default for anyone above 6th place.

## Cadence
Phase 0: as many runs as a week allows. Phase 1: three matches minimum before any gate decision — one match is an anecdote. Phase 2: only after Phase 1 gates, three matches minimum.
