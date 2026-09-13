# ADR-067 — Conceding forfeits the score, in any week

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Owner, 2026-09-11, recorded in `Design/blueprint.md` §9; implemented 2026-09-13 by the first step of `Design/Plans/4X-03-PhaseZero.md`.
**Supersedes:** — <!-- it closes an open question in ADR-023 rather than replacing a decision -->

---

## Context

A player leaves a match in one of two ways. **Absence** is silence: three missed ticks turn the
empire into a custodian, reversibly, and a player whose absence starts inside the first week
forfeits their score for the match (`TickResolver::Resolve`, the presence pass at the top of
phase 1). **Concession** is a declaration: an order that hands the empire to a custodian
permanently, which the one-pager and `Design/blueprint.md` §3 describe as never denying an attacker
their prize — the territory stays on the board and stays takeable.

The two are separate facts and only the first was settled. ADR-023 made score a recomputation of
what a player holds *now* and left open what placement means for a player who conceded; the
resolver has forfeited a concession's score only inside the first week since, using the same test
as absence. The owner answered the open question on 2026-09-11: concession forfeits the score
outright, in any week. `Design/blueprint.md` has said so in the present tense since, with the
disagreement written down beside it, and ADR-023's status line has carried the debt. This ADR is
the record the blueprint pointed at, and the code lands with it.

What makes it matter is placement. A season is scored on placement rather than wins
(`Design/blueprint.md` §1), so the ordering of the players who lost is the whole of what most of
them play for. Under the code as it stood, a player who concedes in week three keeps the score of
the territory they stopped defending and can place above players who were still playing it, while
that same territory decays under a custodian for everyone else to take.

## Options considered

### A. Forfeit only inside the first week — the code as it stood

Concession and absence share one rule and one number (`MatchRules::firstWeekTicks`). The argument
for it is that a late concession is a courtesy: a player who knows they are beaten hands the
board over cleanly instead of leaving a neighbour to besiege an empty chair, and a game that
punishes the courtesy will not get it. It also keeps one concept — "leaving early costs you the
match" — rather than two.

The cost is the placement inversion above, and a rule that reads as a technicality: the player is
told their empire is gone permanently and not told their score is untouched.

### B. Forfeit outright, in any week

One sentence, no window, and the declaration means what it says: conceding gives up the match,
not just the empire. It separates concession from absence, which is honest, because they are
different acts — one is a decision and the other is a life happening.

The cost is real and is named in *Consequences*: after the first week, conceding is strictly worse
for the conceding player than walking away silently, so the rule discourages the tidiest exit.

## Decision

**Conceding forfeits the empire's score outright, in any week.** `forfeitedScore` is set
unconditionally when a concession is applied in phase 1; absence keeps its first-week window and
its own rule, unchanged. The concede row in the signal picker says the cost on the line a player
reads before the second tap (ADR-064): *To a custodian, permanently - your score is forfeit*.

## Consequences

A conceded empire scores nothing at the fixed ending, whatever it still holds, and its placement
falls to the bottom of the order with the other forfeits. Nothing else about concession changes:
it is still permanent, still custody rather than removal, still leaves every system takeable, and
`Conceding never denies an attacker their prize` still holds.

**It makes the graceful exit the expensive one.** After the first week, a player who is beaten now
has a cheaper option than conceding: stop logging in. Three ticks of silence reach the same
custodianship, reversibly, with the score intact. The rule therefore buys honesty in the placement
table at the price of pushing a departing player toward silence, and silence is what H2 of the
playtest plan measures. Phase 1's log already separates *conceded* from *became a custodian*
(ADR-030), so the shape of this is measurable rather than theoretical, and `Design/blueprint.md`
§8 already asks the Phase 1 log to distinguish the kinds of leaving. **This is the first thing to
revisit if Phase 1 shows concessions at zero.**

It also makes the concede row the only control on the main page that costs a player something
irreversible without an opponent doing anything, which is why the arming rule of ADR-064 stands
where it is and why the row now states the cost rather than implying it.

## What this changes elsewhere

- **Design/**: `blueprint.md` §3 (the parenthesis recording that the code had not caught up),
  §4 (the *Concession* row leaves *Designed, not built*), §9 (the follow-up closes and cites this
  ADR). `ADR-023`'s status line records that this question is now closed here.
- **Code**: `GameLogic/TickResolver.cpp`, the concession branch of phase 1;
  `Lockstep/SnapshotView.cpp`, the concede row's second line.
- **Tests**: `GameLogicTests/PlayerTests` gains `ConcedingInWeekThreeForfeitsScore`;
  `LockstepTests/SignalTests` gains a check that the row says what it costs.

## Open questions

**Whether a player should be able to take a concession back inside one lock.** The order is
editable until the lock like every other order, so a mis-tap is recoverable for as long as the
tick lasts, and ADR-064's arming is the guard against the mis-tap itself. Whether that is enough
is a Phase 0 observation rather than a decision to take now.

**Whether absence should follow.** This ADR deliberately leaves absence alone: the first-week
window stays. The owner has chosen (blueprint §9) to keep the absence rule as designed and let
Phase 1 decide, and nothing here pre-empts that.
