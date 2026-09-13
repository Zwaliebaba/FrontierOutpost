# ADR-072 — The first-week forfeit is measured from when they stopped, not from when we noticed

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session, from a finding in the ten-match bot probe of the same day.
**Supersedes:** —

---

## Context

"A player who goes custodian in the first week scores nothing for the match; it is the only cost
that reaches someone who has already stopped playing" (`Design/blueprint.md` §3). The resolver
applied it by testing the tick custody was CONFIRMED against `firstWeekTicks`.

Custody is confirmed `custodianAbsenceTicks` after the last tick the server saw the player. So the
rule really asked *did we find out inside the first week*, and whether it can fire at all depends
on two numbers that are set independently and for different reasons:

| | `custodianAbsenceTicks` | `firstWeekTicks` | Earliest custody | Can it fire? |
|---|---|---|---|---|
| Authored | 3 | 28 | tick 3 | yes, for departures before tick 25 |
| `PhaseZeroRules` | 18 | 16 | tick 18 | **never** |
| `PracticeRules` | 30 (= match length) | 10 | — | no, because custody itself cannot happen |

**Under the Phase 0 preset the rule could not fire at all.** Found by playing ten bot matches under
each rule set on 2026-09-13: the absentee finished on 0 under the authored rules and on 35 under
Phase 0, holding the same territory in both.

Neither number is wrong on its own. `firstWeekTicks` is a fraction of the match because leaving
early is about how much of the match you spoiled. `custodianAbsenceTicks` is scaled by the CLOCK and
not by the match, deliberately and with a comment explaining why: three ticks is eighteen hours at
the authored six-hour cadence, and eighteen ticks is the same eighteen hours at Phase 0's hourly
one, because absence is about how long a person has been away from their life. They simply cross.

**Phase 0 is the playtest that was meant to judge this rule.** `blueprint.md` §8 names absence as
punished harder than a life allows and says H2 will be measured on people the rule has already
pushed out — and Phase 0 would have been the one match in which it did not exist.

## Options considered

### A. Change the preset's numbers

Lower `custodianAbsenceTicks`, or raise `firstWeekTicks`, until the window contains the
confirmation delay. Lowering absence undoes its own reasoning: twelve ticks at an hourly cadence is
twelve hours, which is a night's sleep plus a commute, and the preset exists to stop the rule
saying that a player who slept has stopped playing. Raising the window to 34 of 48 ticks makes "the
first week" mean the first two-thirds of the match, which is a name that has stopped describing
anything.

It also fixes one preset and leaves the trap for the next: any future rule set where confirmation
outlasts the window kills the rule silently again.

### B. Refuse such a rule set in `Check`

`RulesProblem` exists for settings under which a mechanic ceases to exist, and this is exactly one.
It would have caught this. It makes the bug loud rather than absent, and it cannot express the
`PracticeRules` case — where the forfeit is also unreachable, but deliberately, because custody
itself cannot happen in a match played in one sitting.

### C. Measure from when the player stopped

Test `lastActiveTick` — the last tick the server saw them, which the simulation is already told and
already stores — against `firstWeekTicks`. The rule then asks the question the design states: *did
they leave inside the first week*, and it asks it identically at every tick length and match length.

## Decision

**The forfeit is measured from `lastActiveTick`.** A player forfeits their score when they become a
custodian by absence and the last tick they were seen falls inside `firstWeekTicks`.

No preset changes. No new `RulesProblem`: option B would detect a class of bug that option C does
not have, and a check for an impossible condition is a check nobody can ever act on.

## Consequences

The rule is now independent of how long confirmation takes, so it means the same thing under every
preset and under whatever numbers Phase 0 chooses. Phase 0 can judge it, which is the point.

**The authored behaviour barely moves**, which is what makes this a correction rather than a change
of design: it used to catch departures before tick 25 of 84 (custody confirmed before 28), and now
catches departures before tick 28. The scripted match's hash is unmoved — its absentee is absent
from tick zero and forfeits under both readings.

**Two tests were asserting the old reading without meaning to.** Both wound the clock forward with
`SetTick` and then went absent, leaving `lastActiveTick` at zero — a player who never turned up at
all — while claiming to test somebody who left late. They now play the first week before they stop.
That is worth naming, because a test that sets up a state no server could report is a test that can
agree with the code and with nothing else.

**A player who never appears is indistinguishable from one present at tick zero**, since
`lastActiveTick` starts at zero. Both forfeit, which is the right answer for both, so nothing rests
on telling them apart today.

## What this changes elsewhere

- **Code**: `GameLogic/TickResolver.cpp`, the absence branch of phase 1.
- **Tests**: `GameLogicTests/PlayerTests` gains `TheFirstWeekForfeitFiresUnderThePhaseZeroPresetToo`;
  `ACustodianAfterTheFirstWeekKeepsTheirScore` and
  `AbsenceAfterTheFirstWeekStillKeepsItsScoreWhileAConcessionDoesNot` get the first week they
  claimed to have played.
- **Design/**: `blueprint.md` §3's sentence is unchanged and is now true of every preset; §8's note
  about Phase 1 measuring H2 on people this rule pushed out now also holds for Phase 0.

## Open questions

**Whether `firstWeekTicks` should scale by the clock rather than by the match.** It is a fraction of
the match today, which is defensible — spoiling the first third of a weekend match is spoiling the
first third of it — but so is the argument that "a week" means seven days at any cadence. Phase 0
will produce the first evidence either way.

**Whether a player who never joins at all should be treated as a departure.** They forfeit a score
they never earned, so it costs nothing today; it would matter if a seat could be filled late.
