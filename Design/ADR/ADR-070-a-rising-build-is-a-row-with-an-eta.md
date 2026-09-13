# ADR-070 — A rising build is a row with an ETA, and not a marker on the map

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session for `Design/Plans/4X-03-PhaseZero.md`, Step 4.
**Supersedes:** —

---

## Context

ADR-069 made a building take ticks to rise, which gives the client something it has only ever had
for a fleet: a commitment of the player's own that has not resolved yet. The screen already knows
how to say that about a fleet — `FLT3 14 > KEPLER-REACH` on the rail with `T47` beside it, a route
drawn in travelling dots on the map, a card in the digest when it matters — and the question is how
much of that vocabulary a building borrows.

The board is also fuller than it was. A system now offers its NEXT level rather than one of two
buildings, so a six-system empire has up to twelve rows in front of it instead of twelve once; and
a rival who can see a system is told when something starts rising on it (ADR-069), which is one
more card in a digest the design already caps at one per tick.

`Design/blueprint.md` §8 carries the risk this sits under: H4 of the playtest plan kills at a median
session over sixty minutes, and the build menu growing is the most likely way to get there.

## Options considered

### A. Everything a fleet gets, including the map

A marker on the node — a ring, a stem, a count of ticks — so a player reading the map sees what is
rising without opening anything. It is the most informative and it is the most drawing: the node
already carries an owner colour, a capital mark, a siege mark, a custodian mark and the region's
treatment, at 8 pixels on black (ADR-014, ADR-027). `Design/UI/README.md` keeps a list of what the
map does not draw yet — owner tags, the contact spotlight, a verdict label, the approach lane — and
every one of those has a stronger claim on the node than a build does.

### B. The rail, the sheet and the digest, and not the map

The three surfaces that are already lists. The rail is the receipt of what this player has
committed to, so a paid build with an ETA belongs on it beside a fleet under way. The sheet is
about one system, so it is where "it cannot take another order until this lands" belongs. The
digest reports what changed, so it carries the start, the completion and the loss.

What it costs: a player who wants to know what is rising on a system reads it by tapping the
system, rather than seeing it on the map.

## Decision

**B.** A rising build is a row, in three places, and the map draws nothing for it.

- **The rail** lists every construction on a held system after the queue — `MINING L2 JANDAL` with
  `T47` in the status column, in the muted ink a fleet's ETA uses — and the row links to that
  system's sheet, like every other rail row (ADR-060). The queue is what THIS lock will take; the
  rising rows are what an earlier one already did.
- **The sheet** replaces that system's offers with one row that is not a target: the title, `DONE
  T47`, and the sentence *It cannot take another order until this lands*. A row that looked live and
  did nothing is a defect this screen has been bitten by twice (ADR-058, ADR-065), so it is drawn as
  the reason rather than left out.
- **The digest** gets the four kinds ADR-069 emits. Started and completed are economy-coloured; lost
  wears the loss colour and ranks with a system lost, because the credits are gone and there is
  nothing to answer; a rival's is economy-coloured with that rival as its actor, so actor grouping
  folds it under them (ADR-034).
- **A rising row is not an offer.** It is excluded from `availableBuilds` — the rail's `N AVAIL`
  counts what can be started — and the digest never puts a button on one.
- **A build row says what the level buys and what it costs in ticks** on its second line, from the
  snapshot's tables (ADR-053, ADR-069). That is the pair of numbers a player weighs a shipyard level
  against a mining level with, and the client must not know either.

## Consequences

The rail grows by one row per construction and the sheet shrinks to one row per building system,
which roughly cancels: a player mid-build reads fewer rows on the sheet and one more on the rail.
The `+N MORE THAN THIS SHEET CAN SHOW` line absorbs the rest, as it did before.

**The map does not say what is rising**, so the tell ADR-069 created is weaker than it could be: a
rival is told once, in the digest, on the tick it starts, and after that has to remember or tap. If
Phase 0 says the tell is not landing, the map marker in option A is the fix, and it goes on the UI
record's list of what the map does not draw rather than being forgotten.

The session gets longer. That is the point — there is more to decide — and it is also the risk §8
names, which is why Phase 0 should time the session with levels in before anything else is added.

## What this changes elsewhere

- **Code**: `LockstepClient/MainPage.cpp` (the sheet's rising row and level detail, the rail's
  in-flight rows), `Lockstep/SnapshotView.cpp` (`availableBuilds` counts what can be started, the
  digest's offer logic skips a rising row).
- **Tests**: `LockstepTests/SignalTests` gains `OrderingALevelPutsItsETAOnTheDigestAndTheRail`;
  `DigestViewTests` gains the rival-grouping and loss-ranking cases; `TapTests` gains
  `ASystemAlreadyBuildingOffersNothingToQueue` and `ABuildRowCarriesItsLevelAndWhatItTakes`.
- **Design/**: `UI/SCREENS.md` 01 and `UI/README.md` (including the map's list), `GETTING-STARTED.md`,
  `blueprint.md` §2.

## Open questions

**Whether the map should mark a system that is building**, which is option A and the first thing to
try if the tell does not land in Phase 0.

**Whether the rail should show a rival's rising build** as well as the player's own. It is on the
snapshot for every live system, so the data is there; today the rail is strictly "what I have
committed to", and mixing a rival's commitments into it would change what that column means.
