# ADR-064 - Concede is not a neighbour of Hold fire

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, in the 2026-09-12 build prompt: *"`Concede` gets its own 22px section band `CONCEDE` above it, drawn after the other rows, so it is never a neighbour of `Hold fire`."*
**Supersedes:** - (amends ADR-039's signal picker)

---

## Context

ADR-039 made the signal picker a list of rows from a fixed vocabulary and put `Concede` last on it,
needing two taps, with the row itself saying `TAP AGAIN TO CONFIRM` between them. ADR-052 then gave
every sheet row 44 pixels and a column of one height, because "a column of rows of one height is
what a finger aims at".

Both are right and together they produce the thing this decision is about: `Concede` is the last row
of a uniform column, 44 pixels below `Hold fire 3 ticks - P2`, drawn in the same weight and the same
colour as it. A thumb that is one row out hits the row that ends the match instead of the row that
proposes a truce. The second tap is what stops that becoming a concede — and the first tap has
already put the sheet into a state the player did not ask for, on the one order in the game that
cannot be taken back after it resolves.

The two-tap arming is a good guard and it is doing all of the work. Nothing on the row says, before
it is touched, that it is different in kind from the five above it.

## Options considered

### A. Leave it

The arming is genuinely sufficient: one tap cannot concede, and `LockstepTests` pins that. What it
does not do is tell a player why the row they just armed is armed, or stop the mis-hit that armed it.

### B. A confirm dialog

`ConnectionDialog` exists and could carry "concede?" with two buttons. It is the heaviest thing on
the screen, it covers the board, and it replaces a guard that works with a modal — the pattern
ADR-039 deliberately avoided, where the row itself is the warning.

### C. Separate it, and colour it

A 22-pixel `CONCEDE` band between the offers and the row, and the row said in the loss colour from
the moment it is armed.

## Decision

**C.** `Concede` is drawn under a section band of its own, and it is red once it is armed.

**The band is 22 pixels**, which is what a section header costs everywhere else on this screen, and
it carries the label `CONCEDE` in muted text with a rule above it. It is not a target. What it buys
is the 22 pixels of separation: the row above `Concede` is now a band rather than an offer, so no
mis-hit by one row-height reaches it.

**The row is red from the first tap.** Armed, its title and its `TAP AGAIN TO CONFIRM` are both in
the loss colour — the status was amber, which is the warning colour and is also the countdown, the
deadline chip and a pending proposal. Red is `RECONNECTING`, a captured system and a queued concede
on the locks rail, and it means the same thing here. The queued state stays red for the same reason:
it is the same order, one tap further on.

**The band counts against the six-row cap, and loses to a real row.** `SHEET_MAXIMUM_ROWS` is six
(ADR-052) and a label must never cost a control its place, so the band is added only when it and the
row it labels both fit. Beyond that the red text carries the warning alone, which is the graceful
half of the degradation: the colour survives, the spacing does not.

**Nothing about the order changes.** Two taps, armed then queued, taken back by a third, exactly as
ADR-039 wrote it.

## Consequences

- The commonest way to nearly concede — aiming at the last offer and landing one row low — stops
  being possible, because the row one above `Concede` is now a label.
- **A full sheet loses the band**, which is when the picker is busiest and the separation would be
  most useful. That is the cost of the cap and it is chosen over hiding a row.
- `SheetRow` gains two fields (`band`, `alarm`) and the sheet's list height becomes a sum rather
  than a multiply. Every panel shares that code (ADR-052's consequence), and neither field is set
  by the build, destination or replay sheets.
- The signal sheet is one row shorter for the same content whenever the band fits, so a picker with
  four offers and a concede is 22 pixels taller than it was.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01's signal sheet; `DESIGN-GUIDELINES.md`'s sheet component
  gains the band.
- **Code:** `LockstepClient/MainPage.{h,cpp}` — `SHEET_BAND_HEIGHT`, `SheetRow::band`,
  `SheetRow::alarm`, the signal case of `DrawPanel` and the row loop.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `ConcedingTakesTwoTapsOnTheSameRow` still passes unchanged, which is the claim that
matters — the row moved and the guard did not. `AFullSheetDropsTheBandRatherThanTheConcede` builds a
picker of exactly six rows and pins that the concede is still reachable, which is the band losing to
a real row. Run on the client: the band draws between `Hold fire` and `Concede`, and the first tap
turns the row red.

## Open questions

**Whether the other sheets want section bands.** The build sheet lists two buildings on one system
and the destination picker one row per lane; neither has two kinds of row to separate today.

**Whether `Concede` should be reachable at all from a sheet that is full.** With more than five
offers it is beyond the cap and reported in `+N MORE THAN THIS SHEET CAN SHOW`, which is ADR-052's
overflow rule applied to the one row it is least suited to. ADR-039 said the concede is "never
trimmed away" from the composed list and the sheet's cap can still hide it.
