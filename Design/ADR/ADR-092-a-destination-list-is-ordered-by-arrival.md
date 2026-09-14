# ADR-092 - A destination list is ordered by arrival, and says whose ground it is

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 3.5 at the owner's instruction to work the plan.
**Supersedes:** - (extends ADR-063)

---

## Context

The destination picker composed one row per lane in the order `Galaxy::Lanes()` stores them, which
is the order the generator produced and means nothing to a player. ADR-063 put the right facts on
each row -- who holds it, what is standing there, `+DEF` -- and left them in that order, in body ink.

Two things follow from that. **The first thing a player weighs is when the fleet lands**, and the
`2 TICKS · ETA T9` that says so is on the right of every row in a list that is not sorted by it. And
`P3 · 11 +DEF` in muted body ink reads as a number rather than as a rival, on the one sheet where
which rival it is decides whether the tap is an expansion or a fight.

## Decision

**Nearest first, then by name.** A stable sort on the lane cost, with the name as the tiebreak so
two lanes of the same length do not reshuffle between two frames of one state -- the same
determinism requirement the map's labels have (ADR-090).

**A held candidate's second line is drawn in the holder's colour.** `SheetRow` gains `detailInk`,
defaulting to muted; the destination rows set it from `OwnerColor` when the system has an owner.
Unclaimed stays muted, because `UNCLAIMED` is about nobody.

**`detailInk` is the LAST field of `SheetRow`, and that is load-bearing.** Most rows on most sheets
are built with positional braces; a field inserted in the middle silently rebinds every one of them.
It did, and the only reason it was caught is that a `std::int32_t` target will not narrow into a
`Color`. The field's own comment says so.

## Consequences

- **The picker's rows move when a lane's cost changes**, which nothing does today; a trade lane does
  not shorten a lane.
- **A rival's colour now appears on a sheet**, where colour had been the owner square's job alone.
  That is one more place to learn the same twelve colours rather than a new vocabulary (ADR-027).
- The sort is on the ROW rather than on the lane, so `sortBy` is a field on `SheetRow` that every
  other sheet leaves at zero and thereby keeps its composed order. That is a small cost to keep one
  sort in one place.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01's Destination entry. Done in this commit.
- **Code:** `LockstepClient/MainPage.cpp` (`SheetRow::detailInk`, `SheetRow::sortBy`, the
  destination rows and their sort, the row draw). Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `ADestinationSheetPutsTheNearestFirst` opens the picker from the rail and pins that
the lanes it is built from exist to be sorted; the order itself is what the sheet draws, and the
draw is a capture. All 154 methods pass.

**Not photographed** -- captures are being taken as one pass at the end of UI-01.

## Open questions

**Whether the verdict belongs here after all.** ADR-063 wanted `YOU WIN` / `HOLD` / `YOU LOSE` per
candidate and could not have it: the client must not compute a fight, and the snapshot carries a
preview only for a fleet's actual destination. Sorting by arrival makes the list easier to read and
does not make that question any less open.
