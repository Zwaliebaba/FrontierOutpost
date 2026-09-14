# ADR-086 - A fleet row says what differs, and the band says the rest

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 2.3 at the owner's instruction to work the plan.
**Supersedes:** -

---

## Context

The locks rail's `FLEETS` section drew one row per fleet: `FLT 13 10 HOLD HOLLIS`, with the whole
label in one ink and a right-hand column reading `HOLD`.

On a board with ten fleets -- `Design/UI/screens/01-main-page.png` -- that is ten rows in which the
word `HOLD` appears ten times, five system names appear twice each, and the two numbers that do
differ, the fleet's id and its ship count, sit at identical weight with a space between them. The
thing a player is actually scanning that column for is **which of my systems is strong**, and it is
the thing the column says least clearly: the ship counts are scattered down the middle of ten rows,
never added up.

ADR-079 has just put that total on the map as a garrison badge. The rail and the map were saying the
same fact in two shapes, and only one of them was adding it up.

## Options considered

### A. Leave it, now that the map has the badges

The badge answers "where am I strong" better than any list can, so the rail could stop trying. What
that ignores is that the rail is the only place a fleet can be found by NAME, and since ADR-077 it is
the surface a fleet is ordered from -- so it is read, and it is read while deciding.

### B. Sort by system and keep one row per fleet

Cheapest. `FLT 3 3 HOLD ULME` twice in a row is still the system name twice and `HOLD` twice; sorting
groups them visually without removing anything.

### C. A band per system, and rows that carry only what differs

The system name and its total go on a muted band; the rows under it are `FLT 3 · 3` with the id
muted and the count primary, and carry no right-hand column at all.

## Decision

**C.** A `FLEETS` section is a list of bands: one per system the viewer holds something at, reading
`ULME · 7 SHIPS`, then a row per fleet under it. Everything in transit goes under one `UNDER WAY`
band, because a lane is not a place and the thing those fleets have in common is that none of them
can be ordered (ADR-077).

**`HOLD` is gone.** It said "this fleet is not moving", which is now said by which band the row is
under. The one thing standing still does NOT imply is kept: `+DEF` in blue, because being the
incumbent is a fact about the next fight rather than about the fleet's stance.

**The id is muted and the count is not.** `FLT 3` is how you name the fleet when you talk about it
and `3` is what you weigh it by. `row` gained a `_dimHead` byte count rather than a second row
lambda, so the hover, the hit rectangle and the wrap stay in one place -- a second copy of those is
how two rows end up disagreeing about where they are.

**The section header's count is unchanged.** `FLEETS 10` counts fleets, not bands; the bands are how
the ten are arranged and not a different ten.

**The band's total is the badge's total.** `ULME · 7 SHIPS` is the number `GarrisonsAt` draws on the
map (ADR-079), computed the same way from the same list, so the two surfaces cannot disagree about
how strong a system is.

**Row link behaviour is untouched**, and this ADR says so because UI-01's text for this item said the
opposite: it described a holding row as focusing and an under-way row as opening the picker, which
is what ADR-077 inverted. A holding row opens the picker, an under-way row focuses where it is
going, and at the lock everything focuses.

## Consequences

- **A player with one fleet at one system now spends two lines where they spent one.** Measured in
  the client at T8: `FLEETS 1` / `DOTHAN · 10 SHIPS` / `FLT 1 · 10`. That is the cost of the format
  and it falls hardest on the opening board, where the rail has the most room to spare; it pays back
  from about the fourth fleet and pays back most at ten.
- **The rail is taller overall**, by roughly a line per system held. The rail does not scroll or
  page -- ADR-080 gave that to the digest column only -- so a late-game empire can now run its
  sections off the bottom sooner. That was already possible and is now nearer; see the open question.
- A band is not a target, so nothing new is tappable and the hit count per fleet is unchanged.
- The `+DEF` blue is now the only colour in the section, which makes it louder than it was among ten
  `HOLD`s. That is the right direction: it is the only row that is about a fight.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` "Locks list row" and the rail's description in `SCREENS.md` 01.
  Done in this commit.
- **Code:** `LockstepClient/MainPage.cpp` (`row` gains `_dimHead`, a `band` lambda, and the `FLEETS`
  section rewritten). Done, built and photographed.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: the rail's existing tap tests pass unchanged, which is the claim that matters --
`LocksRailTapTests` and `FleetMoveTapTests` sweep for the control rather than for a coordinate, so
they survive the new geometry and would fail if a row stopped doing what it did. All 138 methods in
the suite pass.

Photographed on 2026-09-14 at T8 against a `--serve --phase0 --bots 5` server: the band reads
`DOTHAN · 10 SHIPS` in muted ink, the row under it `FLT 1 · 10` with the id muted and the count
primary, and no right-hand column. The map's badge beside Dothan reads `10`, the same number.

## Open questions

**What the locks rail does when it runs out of column.** It has never had an answer -- it draws
downward from the top and stops when the screen does -- and this item makes a tall rail likelier. The
digest's answer (ADR-080) is a scroll, and the rail is the obvious second candidate; it is a bigger
change than this item and nothing has run off the bottom in a capture yet.

**Whether the bands should carry the system's colour.** They are muted text today. A band in the
owner's blue would tie the rail to the map's badge more tightly, and would also put a fifth use of
blue on a screen that already carries four.
