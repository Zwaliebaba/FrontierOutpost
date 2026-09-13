# ADR-069 — A building has three levels and takes ticks to rise

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Owner, in the design session of 2026-09-13 recorded in `Design/blueprint.md` §3 and §9; implemented the same day by Step 3 of `Design/Plans/4X-03-PhaseZero.md`.
**Supersedes:** —

---

## Context

Building was a checklist rather than a decision. A player could build a shipyard (20 credits) and a
mining station (15) once each per system, and a trade lane (10) per border; every one landed at the
lock it was ordered at and never changed again. A starting empire of three systems builds out for
105 credits against a purse of 100 and an income of about twelve a tick, so by roughly day two the
purse only grows and the one thing left to spend on is a ten-credit lane. **The number in the top
bar stops meaning anything for the remaining nineteen days of a twenty-one-day match.**

The second half is the design rule the build menu could not keep: *no session may end without
something in flight*. A fleet gives a player a pending ETA; a build never did. Everything a player
committed to resolved before they closed the app.

`Design/blueprint.md` §3 records the design session that answered both, and what it takes from
Civilization and leaves there. This ADR is the half that is rules, and it is deliberately the half
that is only numbers and a timer: the bastion and the research track designed beside it wait for
Phase 0's evidence on the combat numbers they lean on.

## Options considered

### A. More building kinds

A third and fourth building — the bastion, a lab — give credits somewhere to go without touching
how building works. It is more to explain on a sheet that holds four rows, it needs combat numbers
Phase 0 has not tuned yet, and it does not put anything in flight: a fourth kind still lands the
tick it is ordered.

### B. Levels, and a build that takes ticks

Three levels per building, each costing more and paying more, each taking ticks to complete. One
change buys both things: a sink that scales with the empire, and an ETA on every build. It reuses
the vocabulary the screen already has — a price on a button, a countdown on a row — rather than
adding a concept.

The cost is that every rule which read a boolean now reads a number, the wire grows six tables, and
the scripted match's hash moves.

### C. Upkeep

Charge credits per building per tick. It is the sink with the fewest moving parts and it is the one
that punishes the player who is already losing: an empire under siege pays the same upkeep on
systems that are about to fall. It also puts nothing in flight.

## Decision

**A building has three levels, and a level takes ticks to rise.**

`MatchRules` carries six per-level tables — cost, build ticks and yield, for the shipyard and the
mining station — and `LevelValue` is the one place `[level - 1]` is written. A system carries
`shipyardLevel` and `miningStationLevel` (0 being none) and one `Construction { kind, toLevel,
completesAt }`. `Check` refuses rules where a level does not cost more AND pay more than the one
below it, or where any level takes no ticks: a level that is free is not a decision, and a build
with no ETA is the thing this ADR exists to remove.

Inside that, five choices that had alternatives:

1. **One construction per system at a time.** A second order is refused with `AlreadyBuilding`,
   whatever its kind. A per-system queue is more state on the wire, a second thing for the sheet to
   explain, and a floor plan by another name — which the one-pager rules out by name.
2. **A capture cancels what was rising, and the credits are gone.** Handing a half-paid level to the
   besieger is a prize the siege did not earn; refunding it is a siege that costs the defender
   nothing. The loser is told, which is what makes it a cost rather than a surprise.
3. **A custodian's paid construction still completes.** Custodian territory "defends, never expands,
   never attacks" — and a level already paid for is neither an expansion nor an attack. Cancelling
   it would take credits from somebody who has already stopped playing.
4. **A construction completes at the top of phase 1 and therefore produces in phase 2 of the same
   tick.** It lands where a build has always landed, so a one-tick level is exactly one lock later
   than the old behaviour and nothing else about the tick moves.
5. **The cost, tick and yield tables travel on the snapshot.** The sheet's whole job is to let a
   player weigh a shipyard level against a mining level, and it cannot do that without both numbers
   (ADR-053: the client owns the sentence, the server owns the number).

**Level three is a number in this ADR, not a feature.** `Design/blueprint.md` §3's table sketched a
feature at level three — a mining station doubling its lanes, a shipyard repairing a fleet. Ships
have no damage state, so the second means nothing today; the first is unlock-shaped and belongs with
the research track. Level three is the top of the same curve, and the blueprint's table is amended to
say the features arrive with the bastion after Phase 0.

## Consequences

Credits have somewhere to go for the whole match: a six-system empire that has built everything to
level one still has 300 credits of levelling in front of it. Every build is a commitment with an ETA
on the rail, and a rival who can see the system sees it rising — a tell of exactly the kind the
design already trades in, and the reason `Construction` is reported only for a live system, never a
remembered one (ADR-022).

**The scripted match's hash moves**, from `0xC204BAED2104E2E7` to `0x22B59450A1D76F74`, because every
bot now spends differently and every building pays one tick later. Re-pinned under Debug and Release,
which both agree. **Every stored match written before this is unloadable** (ADR-024); no match is
being kept, since Phase 0 has not started.

The wire grows: six three-element tables on the rules, six on the snapshot, and five words per
system. `MatchRules` is 208 bytes where it was 152. The rules header now carries `BUILDING_LEVELS`
beside the field count, because a table is one field however long it is and the field count alone
cannot catch a change to the level count.

**The build menu is now the longest it has been**, and `Design/blueprint.md` §8 carries the risk that
follows: H4 kills at a median session over sixty minutes, and Phase 0 should time the session with
levels in before anything else is added.

## What this changes elsewhere

- **Code**: `MatchRules.h` (tables, `LevelValue`, two new `RulesProblem`s), `Match.h`/`Match.cpp`
  (levels, `Construction`, validation, hash), `Orders.h`/`Orders.cpp` (`AtTopLevel`,
  `AlreadyBuilding` replacing `AlreadyBuilt`), `TickResolver.cpp` (completion at the top of phase 1,
  starting a build, production by level, capture), `TickLog.h`/`.cpp` (four build digest kinds and
  `RIVAL_BUILDING`), `Snapshot.h`/`.cpp` (levels, the rising build, the six tables), `BotPolicy.cpp`
  (cheapest affordable next level), `MatchSimulation.cpp` (archiving tables, the level guard, the
  size tripwire), `SnapshotView.cpp` and `MatchState.h` (rows that offer the next level).
- **Tests**: `GameLogicTests` gains `BuildLevelTests` (nine), two snapshot visibility tests and two
  bot tests; the scripted hash is re-pinned.
- **Design/**: `blueprint.md` §3 (level three), §4 (the row moves to what exists today), §9 (the
  follow-up closes).

## Open questions

**What level three should eventually open**, beyond a bigger number. It is the same question as the
research track's list of unlocks and should be answered with it, after Phase 0.

**Whether a construction should be cancellable by its owner** for a partial refund. Today it is not:
credits are spent at the lock and the only way to stop a build is to lose the system. Nobody has
asked for it and Phase 0 will say whether anybody wants to.

**Whether the numbers are right.** They are initial values — 20/40/60, 15/30/50, one to three ticks
— and Phase 0 exists to change them. The shape is what this ADR fixes.
