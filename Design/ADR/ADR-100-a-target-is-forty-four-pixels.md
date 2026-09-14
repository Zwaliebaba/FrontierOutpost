# ADR-100 - A target is forty-four pixels, and how it gets there depends on what it sits beside

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision, answering `Design/Plans/UI-02-TouchTargets.md`'s first two [ASK]s: 44 rather than 32, and grow the hit rather than the chip where a chip is isolated.
**Supersedes:** -

---

## Context

ADR-098 recorded that this game is for touch and that almost nothing on the screen is a touch
target. It deliberately stopped there, at the record, and left the number and the method to a plan.
This is that decision.

Two questions had to be answered before any constant moved.

**Which number.** UI-01 3.8 said "at least 32". `SHEET_ROW_HEIGHT`'s own comment -- the one place
somebody had already applied a floor -- says 44 is *"the smallest target a finger hits reliably"*.
Both numbers were already in the tree, saying different things.

**What to grow.** A locks-rail row is one of a column of siblings: making its box 44 moves the rows
below it and nothing else. A garrison badge is not: it sits beside a system name on the map at a
size chosen to fit that label's line, and a 44-pixel badge is a different map.

And one thing had to be established before either answer could be trusted: **reading the constants
is not evidence.** A control's box is composed from several of them -- a padding, a line height, a
`BandTopForText` -- and the one that goes wrong is the one nobody added up. ADR-098's own table was
read that way and was close enough to be useful and wrong in detail.

## Options considered

### A. 32 pixels everywhere

Fits the current layout almost as it stands: a 32-pixel digest button keeps two buttons on a card at
400px wide. It is also a number with nothing behind it -- UI-01 wrote "at least 32" as a floor, not
as a finding, and the tree's own comment says 44.

### B. 44 pixels, grown differently by kind

The number `SHEET_ROW_HEIGHT` already uses, applied by what a control is:

- **A target in a column of siblings grows its box.** Rail rows, card buttons, sheet rows, the page
  bands. The column gets taller and the things under it move down, which is a layout change and is
  the point.
- **An isolated chip grows only its hit.** The garrison badge stays 16 pixels on the map with a
  44-pixel rectangle centred on it; so do the `REPLAY` and `RESET` chips on the top bar. Nothing
  moves, and a finger that lands anywhere near the badge hits it.

### C. 44 pixels everywhere, boxes only

Consistent, and it makes the map a grid of badges rather than a map. Rejected on sight.

## Decision

**B.** The floor is **44**, and how a control reaches it depends on whether it has siblings.

**The audit is a test, not a table.** `Tests/LockstepTests/TouchTargetTests.cpp` draws the real
screens through the headless renderers and measures what `AddHit` actually recorded, **in both
dimensions** -- a 260x21 rail row and a 15x16 garrison badge fail for different reasons and a
height-only audit finds one of them. It names each offender by its `Action`, so a failure says which
control rather than which rectangle.

It found five at tick zero, which is three more than ADR-098's table predicted and one fewer in the
place the table was most confident.

## Consequences

- **The main page's layout changed.** Rail rows and section headers are 44, the digest card's
  buttons are 44, and every capture of the main page is stale.
- **It caught a defect the first fix caused.** The card's action row reserved one `LINE_HEIGHT`
  while `BandTopForText` centred the button on that line's baseline -- true enough at 18 pixels, and
  at 44 the button reached a whole line above its row and painted over the detail line there. That
  was found in a **screenshot**, not in the test: the audit measures hit rectangles and this was a
  drawing that did not match one. The row now reserves `BUTTON_HEIGHT` and centres the label inside
  it.
- **The rail overflows now.** 44-pixel rows in a column that was sized for 21-pixel ones means a
  played empire runs off the bottom, which is ADR-101.
- **A control that cannot be 44 has to say why where it is declared.** The garrison badge's
  declaration says it, and so does every other isolated chip.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Frame (the floor and the two methods),
  `Design/Plans/UI-02-TouchTargets.md`. `Design/UI/SCREENS.md` for the main page's new row heights.
- **Code:** `LockstepClient/MainPage.{h,cpp}` (`TOUCH_FLOOR`, `BUTTON_HEIGHT`, `RAIL_SECTION_HEIGHT`,
  `DIGEST_PAGE_HEIGHT`, the action row's reservation), `LockstepClient/MapRender.cpp` (the badge's
  hit).
- **Tests:** `Tests/LockstepTests/TouchTargetTests.cpp` (new).
- **AGENTS.md:** nothing.

## Verification

`TouchTargetTests` over five boards -- the opening board, a played board, a sheet over the board, a
locked board and the developer bar -- asserts that no recorded hit is under 44 in either dimension.
Screenshots of the main page at tick 0 and mid-match, which is where the action row's overdraw was
found.

## Open questions

**The sheet's header and close corner are 36.** They are a column of siblings by one reading and a
chrome row by another, and the audit does not fail them because the close corner's hit is already
larger than the glyph. Left as it is until somebody misses one.
