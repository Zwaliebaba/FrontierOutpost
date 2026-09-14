# ADR-093 - The concede is pinned below the cap, not inside it

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 3.6 at the owner's instruction to work the plan.
**Supersedes:** - (amends ADR-064)

---

## Context

A sheet draws six rows and reports the rest as `+N MORE THAN THIS SHEET CAN SHOW` (ADR-052). The
signal picker's rows are the things this player can say to somebody, and the last of them is
`Concede` -- under a band of its own, in red from the first tap, needing two (ADR-064).

ADR-064 put the concede inside the six and then had to defend its place twice: the band was dropped
when the sheet was exactly full, and the row was moved into the last visible slot when it overflowed.
Both are correct and both are the same admission -- **the one control that must always be reachable
was competing for room with the ordinary ones.** On a board busy enough to offer six signals, a
player paid for the ability to concede with a signal they could not send.

## Options considered

### A. Keep defending its place inside the cap

Works, and it is what the tree did. It costs a real row every time the sheet is full, and the
defending is two special cases in the layout that exist only for this row.

### B. Raise the cap for this sheet

Seven rows when a concede is present. The cap is a frame decision (ADR-052: six rows and the sheet
still leaves over half the map pane showing), and making it per-sheet is a worse thing to own than a
pinned row.

### C. Pin it below the six and outside the count

## Decision

**C.** `DrawPanel` composes a second list, `pinned`, drawn after the capped rows and the `+N` line
and immediately above `CANCEL`. The concede and its band go there; nothing else uses it today. The
sheet's six are six real signals again, and the `+N` counts only them.

**One lambda draws both lists.** A pinned row is an ordinary row that is not counted, so a second
copy of the row drawing would be a second place for a band's rule or a row's hit rectangle to drift.
The "no second rule directly under a band" rule became a `previousWasBand` flag rather than an index
comparison, which is what let the two lists share it.

**The two-tap arming and the red-from-first-tap are untouched** (ADR-064). What changed is where the
row is, not what it does.

## Consequences

- **The signal sheet is up to two rows taller than the cap implies** when a concede is present,
  which is every tick: the band and the row are 22 + 44 on top of six 44s. A full signal sheet is
  now 36 + 264 + 66 + 40 = 406 pixels of the 676-pixel pane, so the map is still the larger half.
- **`+N MORE` now counts only signals**, which is what a player would assume it counted.
- ADR-064's two defences are gone, and with them the case where a label cost a control its place.
- Nothing else is pinned. If a second thing ever is, the mechanism is there and the sheet gets
  taller again, which is the moment to revisit the cap rather than to add a third list.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Components (the sheet), `SCREENS.md` 01's Signal entry.
  Done in this commit.
- **Code:** `LockstepClient/MainPage.cpp` (`pinned`, the `drawRow` lambda, the signal panel).
  Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `AFullSheetKeepsItsSixSignalsAndTheConcede` -- renamed from
`AFullSheetDropsTheBandRatherThanTheConcede`, because the band is no longer dropped -- sweeps a sheet
of five signals and a concede and pins both that the concede can be armed and confirmed and that the
fifth signal is still reachable on a fresh page. `AnOverfullSheetStillOffersTheConcede` keeps its
claim about a sheet of nine. All 154 methods pass.

**Not photographed** -- captures are being taken as one pass at the end of UI-01.

## Open questions

**None.**
