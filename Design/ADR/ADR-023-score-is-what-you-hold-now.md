# ADR-023 — Score is what you hold now, and the match ends two ways

**Status:** Accepted — of its open questions, on 2026-09-12: the match stops resolving when it ends — `Session::Advance` stops once the simulation reports it finished, and the server refuses orders for a finished match (`4X-02` §6); what placement means for a player who conceded was decided by the owner on 2026-09-11 — concession forfeits score outright in any week (`Design/blueprint.md` §9) — and the ADR and the code change are still owed, the resolver forfeiting only a first-week concession today; whether a first-week custodian should be told repeatedly is still open.

**Date:** 2026-09-11
**Decided by:** Build session for `Design/Plans/4X-01-CoreLoop.md`, Step 7. The plan asks for the placement tiebreak to be decided and recorded; the surrounding decisions are recorded with it because they are one question.
**Supersedes:** —

---

## Context

The one-pager fixes the ending and leaves the arithmetic open:

> **Public score.** The leader is always visible. Leader-ganging is the anti-snowball; no mechanical
> rubber-banding beyond distance and supply cost.
>
> **A defined ending.** Fixed end date; placement by score. An early dominance threshold ends the
> match only if held for several consecutive ticks, so the leader stays attackable.

And, about scoring: *"exiles score like everyone else, on what they hold at match end"*, and
*"a player who goes custodian in the first week scores nothing for the match"*.

Three things follow that are not obvious until written down. **Leader-ganging is the only
anti-snowball mechanism**, so the score has to be public *and* legible — if players cannot tell who
is winning, they cannot gang up, and nothing else stops a runaway. **"Held for several consecutive
ticks" is load-bearing**: a threshold that ends the match the moment it is crossed would make the
last hour of a three-week match unobservable to anybody who was asleep. And **placement decides
season points** (*"Season points by placement, not by wins... fourth place has to be worth playing
for"*), so ties are not a curiosity — they decide what a player takes away from three weeks.

## Options considered

### Score: accumulated per tick, or recomputed from what is held

**Accumulated** — add each tick's holdings to a running total — rewards holding early, which is
defensible, and is what a 4X usually does. It also makes an early lead nearly impossible to lose:
by mid-match the leader's total is a sum nobody can catch, whatever happens on the board. That
turns off leader-ganging exactly when it is needed, because ganging the leader no longer changes the
score.

**Recomputed** — score is what you hold *now* — means losing half an empire drops you immediately.
The leader stays catchable and ganging works, which is the anti-snowball the design relies on. It
also matches the one-pager's own words: "on what they hold at match end".

### Dominance: a share of the board, or a share of the score

A **share of systems** is simpler to explain but ignores capitals, which are worth holding. A
**share of total score** folds in whatever score already counts, so the threshold moves with the
scoring rule instead of drifting away from it.

### The tiebreak: by what, and how far

The plan says to decide and record. A tie on score is common here precisely *because* score is
recomputed from holdings: six players with one capital each have six identical scores at tick zero,
and that is the state every match starts in.

An **unbroken** tie is not an option: `std::sort` on an incomplete comparator is unspecified, so two
machines reporting the same finished match could print different placements. The order has to be
**total**.

## Decision

**Score is recomputed every tick from what is held.** `scorePerSystem` for each system, plus
`capitalScoreBonus` for a capital. Nothing accumulates.

**A first-week custodian scores zero, permanently.** The flag is set when custody begins inside
`firstWeekTicks` and never clears, even if the player returns and plays out the match. It is applied
to the *reported score*, not to the territory — the territory stays on the board and stays a prize,
because "conceding never denies an attacker their prize".

**Dominance is a share of total score, held consecutively.** `dominanceSharePercent` (initially 60)
of all score on the board, for `dominanceHoldTicks` (initially 4) consecutive ticks. One tick below
the share and the count returns to zero — consecutive is the whole rule, and it is what keeps the
leader attackable.

**The match ends two ways and reports both.** At `matchLengthTicks`, or when somebody completes the
dominance hold. Either way every player's digest gets a `MatchEnded` entry at the top of the sort
(severity 1000, above everything, because nothing that happened this tick matters more).

**Placement is ordered by, in order:**

1. Score, descending.
2. Systems held, descending.
3. Capitals held, descending.
4. Player id, ascending.

The first three are meaningful. **The fourth is arbitrary and is there to make the order total** —
two players who are genuinely identical on every axis the game measures have to be ranked somehow,
and an arbitrary rule that is the same everywhere beats an unspecified one that is not. It is
recorded here so that a future reader knows it is a tiebreak and not a claim that player 0 is
better.

**`MatchRules::Check` refuses a dominance share at or below an even split** — which one player
would reach by playing normally — **or above 100**, which nobody could reach; and a hold of zero
ticks, which ends the match the instant somebody leads.

## Consequences

**A leader can be dethroned on the last tick**, and that is intended. Three weeks of holding the
most systems counts for nothing if you lose them at tick 83. Whether that feels like drama or like
waste is a Phase 0 question; the alternative makes ganging pointless, which is worse.

**Score is cheap to compute and cheap to explain.** One pass over the systems, once a tick. A player
can count their own score off the map, which is what "public score" has to mean in practice.

**The first-week forfeit is invisible until it bites.** A player who lapses at tick 10 and returns
at tick 20 plays out the match with a score of zero and no way to earn one. The digest tells them
when custody begins; whether it should keep saying so is an open question below.

**Ties are common early and rare late.** At tick zero every placement is decided by the arbitrary
rule, which nobody will notice because nothing is at stake yet. By the end, exact ties on score,
systems *and* capitals are unlikely.

## What this changes elsewhere

`MatchRules` grows seven fields. `PlayerState` grows `status`, `absentTicks`, `custodianSince`,
`forfeitedScore` and `dominanceTicks`, all hashed. `Match` grows `Leader()`, `Placements()`,
`IsFinished()` and `DominanceWinner()`.

The top bar on the main page already draws a score, a placement and a leader from the fixture. The
snapshot now carries all three for real; wiring them is the rest of Step 8's client work.

## Open questions

**Whether the match should keep resolving after it ends.** `IsFinished()` is reported and nothing
enforces it — the resolver will happily run tick 85. The server decides when to stop calling it
(`4X-02`), which is the right place, but nothing says so yet.

**Whether a first-week custodian should be told, repeatedly, that they are scoring nothing.** They
are told once when custody begins. A player who returns at tick 20 and plays hard for two weeks
without noticing would have a bad time, and the one-pager's cost is meant to reach someone who
*stopped playing*, not someone who came back.

**What placement means for a player who conceded.** They are a custodian, they still hold territory
until somebody takes it, and they still place. The one-pager says concession "forfeits the empire's
score outright" in the Exile section, which is about a different state; whether it should apply here
is unresolved and is Phase 0's to answer.
