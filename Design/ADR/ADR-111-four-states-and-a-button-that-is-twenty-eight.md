# ADR-111 — Four states, and a button that is twenty-eight

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner, in the place-sheet interaction handoff of 2026-09-14
(`design_handoff_place_sheet/`, sheet `1c`) and the prompt that carries it.
**Supersedes:** — (amends ADR-053's suffix clause and ADR-100's button height)

---

## Context

This client draws four kinds of control and each of them says its state in its own way.

A **digest button** is 18 pixels tall with 6 pixels of side padding, and it says what it is by
growing its label: `BUILD 20 CR` becomes `BUILD 20 CR - QUEUED` when it is in the orders, and
`BUILD 45 CR - NEED 19 MORE` when the purse cannot cover it (ADR-053). A **build tile** is 284×96
and says the same two things on a line of its own, `QUEUED −40` beside `TAP TO TAKE BACK`, in a
seven-state table of its own (ADR-107). A **sheet row** says it in a right-aligned status word —
`SENDING`, `QUEUED -20`. A **rail row** says it in the same slot, in one of five colours (ADR-086).
Four vocabularies for one idea, and the idea is small: *this is yours, it is queued, and tapping it
again takes it back.*

The suffix form has a second failure that is not about consistency. **The digest drops a button that
does not fit its column rather than wrapping it** — a rule this tree has had since the rail was
narrowed — and a suffix makes a button's width a function of its state. `MINING STATION L2 JANDAL
30 CR` is 217 pixels in the mono cut at the 7-pixel column measured for ADR-084; add ` - NEED 19
MORE` and it is 315, against a 372-pixel column that also has to hold whatever sits beside it. So a
build going out of reach can take its own button off the screen, which is the one moment a player
most needs to see it.

And the box is 18. ADR-100 grew every target to 44 by growing the boxes in a column of siblings and
growing only the hit for an isolated chip; a card's action row was read as a column and its buttons
went to 44. They are not a column: they sit side by side with a gap between them, which is the
isolated-chip case, and the 44 spent sixteen pixels of every card on ascender room a shouted label
never uses. Measured on the tick-11 capture, a card with an action row is 26 pixels taller than it
needs to be, which on a 676-pixel column is most of a card.

Two figures bound what follows, both from `FontRenderer::MeasurePixels`, which is `constexpr` and is
the arithmetic the renderer lays out with. The mono column is **7px** at the baked 12px size
(ADR-084 measured it), so a label's cell is `10 + 7n + 10` and a number's is `8 + 7m + 8` with a
1-pixel rule between them. The digest's text column is 372 pixels wide
(`DIGEST_WIDTH − RAIL_PADDING − TEXT_LEFT`).

## Options considered

### A. Keep the suffix, and keep the 18-pixel box

Nothing to build. It costs what is described above: four vocabularies, and a control whose width
depends on a state the player did not choose.

### B. One state table, applied by a shared routine, with the number in its own cell

Four states — primary, outlined, committed, inert — plus the lock, chosen by one function and drawn
by one routine that every surface calls. A button becomes two cells, `label | number`, so the number
is a fixed slot rather than a suffix: `20 CR`, `−20`, `NEED 19 MORE`, `10 SHIPS`, `AFTER T14`. The
box goes to 28 with an 8-pixel gap either side, and the target is grown to 44 around it.

It costs a real amount of drawing code in one place — a state table, a box routine, a button
routine — and it costs every capture with a button on it. It buys a screen where "queued" looks the
same on a button, a tile, a sheet row and a rail row, and where a state change never moves a
control's width by more than the number in it.

### C. One state table, but keep the single-cell button

The states without the segment. It is most of the consistency for none of the layout work, and it
leaves the width problem exactly where it was: `NEED 19 MORE` still has to go somewhere, and the
only place left is the label.

### D. A fifth state for "hovered"

Rejected as a category error. Hover is not what a control IS, it is where the pointer is; folding it
into the state would give twice as many enumerators, each of which has to be kept in step with its
pair. It is a parameter of the table instead.

## Decision

**B.** There is one control vocabulary on the main page, it has four states and a lock, and one
function chooses its inks.

| State | Border | Fill | Ink | Target? | Meaning |
| --- | --- | --- | --- | --- | --- |
| Primary | none (hover: 1px `BLUE` ring) | `BLUE`, hover `BUTTON_PRIMARY_HOVER` | `APP_BACKGROUND` | yes | the one recommended thing to do; **one per screen** (ADR-089) |
| Outlined | 1px `OUTLINE`, hover `OUTLINE_HOVER` | none, hover `HOVER_FILL` | `TEXT_PRIMARY`; number `TEXT_MUTED` | yes | any other order, and every link |
| Committed | 1px `BLUE` | `TILE_COMMITTED_FILL`, hover `COMMITTED_HOVER_FILL` | `BLUE` | yes | yours, queued; the hover flips the label to `TAKE BACK` and the number to `+20` |
| Inert | 1px **dashed** `INERT_BORDER` | none | `NEUTRAL_DIM`; number `AMBER` when the reason is money | **no** | cannot be ordered; the number says why |
| Locked | none | `LOCKED_FILL` | `APP_BACKGROUND` | no | at the lock (ADR-065); a 6px square precedes the label |

**The hue never changes between states.** Only the fill, the border's style and the alpha do, which
is what keeps blue meaning *yours* rather than meaning *this particular state*.

**A dashed border is the only one in this client and it means one thing: not a target.** There is no
dashed control a player can tap, and there is no inert control drawn any other way.

Six things inside that had alternatives.

**1. The number is a cell, not a suffix.** Every priced or counted control splits `label | number`
with a 1-pixel rule between them — `BUTTON_SEGMENT_SHADE` and no rule on a filled button, because a
hairline on a solid reads as a crack; `DIVIDER` on an outlined one; `BLUE` at 90 on a committed one;
the dashed border's own ink on an inert one. **A number never appears inside the label**, and the
digest measures label and number together when it decides whether a button fits its column
(ADR-053's drop rule is unchanged).

**2. The box is 28 and the target is 44, which is ADR-100's chip rule and not its column rule.** A
row of buttons with gaps between them is not a column of siblings: 28 + 8 + 8 is exactly 44, so a
target centred on one button reaches the middle of the gap on either side and no further, and two
neighbours cannot overlap. The gaps are *reserved* in the card's height rather than borrowed from
whatever the line above left, because a grown target reaching into a gap that was not there is the
overlap ADR-100 grew the boxes to avoid.

**3. Tracking stays at zero, and the handoff asked for 0.04em.** At the 12px cut that is 0.48
pixels, and `FontRenderer` advances in whole pixels — there is no sub-pixel advance on a path that
deliberately has no sampler (ADR-011, ADR-075). The two ways to have it were to round to a whole
extra pixel between glyphs, which is 0.04em spelled as 0.083em and visibly loose at this size, or to
add a tracking parameter to a renderer whose `MeasurePixels` is `constexpr` and is called from
thirty layout sites. Neither is worth 0.48 pixels. **Labels do change face**: a button label and its
number are set in mono **Medium**, where they were Regular, which is the emphasis step this screen
already has (ADR-074).

**4. The corners are square, and the handoff asked for a 2px radius.** `ShapeRenderer` has no
rounded rectangle, and adding one is a primitive to keep in step with `StrokeRect` forever — for a
radius that is two pixels on a 28-pixel box, on a screen where the sheet, the tile, the chip and the
dialog are all square. One rounded control family among square everything else reads as a mistake
rather than as a softening.

**5. A hover is a parameter of the table, and it is drawn.** `MainPage` tracked the rail's rows so
that a pointer crossing one could force a redraw (ADR-047); it now tracks every rectangle that
fills under the pointer, because a button whose hover the redraw never noticed would light only when
something else on the screen changed. **A committed control says what the next tap does while the
finger is on it** — `TAKE BACK`, `+20` — and the flip is refused when the new label is wider than
the resting one, so a button under the pointer never pushes the one beside it along.

**6. A locked BUTTON is filled grey; a locked tile, sheet row and rail row dim in place.** The
handoff's table puts `LOCKED_FILL` under every locked control. At the size of a chip that is the
same statement the rail's `LOCKED` chip already makes and it is legible — `APP_BACKGROUND` on the
grey, which `ContrastTests` measures. At the size of a 284×96 tile or a 260-pixel row it is not that
statement at all: a locked sheet would become four light-grey boxes, which is the screen inverted
rather than gone quiet, and ADR-065 settled that a sheet at the lock stays where it is and fades.
The handoff's own ink for the locked state, `NEUTRAL_DIM`, measures **1.14:1** over `LOCKED_FILL`,
so the two halves of that row cannot both be right; the constraint the handoff states — every text
ink clears 4.5:1 over every ground it is drawn on — is what decides which half goes.

## Consequences

**Every card with an action row is 8 pixels shorter.** The row reserved `4 + 44 + 4`; it now
reserves `8 + 28 + 8`. On the tick-11 board that is one more card on the first screenful.

**Two tests changed shape, and neither changed its claim.** `TapTests` sweeps the screen at an
8-pixel step and stops when it sees an effect, which makes the trajectory a function of the layout:
moving a button's target up by four pixels changed which control a row reached first.
`ASheetSurvivesTheLockAndStillTakesNoOrder` now measures whether the queue CHANGED after the lock
rather than whether it is empty, because the sweep that opens a picker on an unlocked board may
queue a build on the way; and `ARivalsSystemOpensNoBuildSheet` now closes whatever sheet the
previous row opened before each tap, using `SweepFor`'s `_ensure` hook, because the signal picker
covers the map from a third of the way down the pane and a system under it can never be reached.
Both are fixture repairs and both are recorded here rather than left to be rediscovered.

**Two tokens were added that the handoff did not name**, `OUTLINE_HOVER` and
`COMMITTED_HOVER_FILL`, because it names their values (`white@102`, `BLUE@36`) and this tree has one
palette rather than literals at draw sites (ADR-045's closed item).

**`EventAction` carries a second string.** What the number IS stays the composer's business —
`SnapshotView` prices a build, `DigestView` counts a fleet's ships — and where it is drawn is the
button's. The alternative was for the button to parse a number back out of a label, which is the
defect `DeltaCell::loss` was introduced to stop.

**The hover is drawn on a screen built for touch** (ADR-098), where most players have no pointer at
all. It costs a rectangle per control in a list that is rebuilt every frame, and it is the only
channel a committed control has for saying `TAKE BACK` before the tap rather than after it.

**Every capture with a button on it is stale.** `01-main-page.png`, `01-orders-queued.png`,
`01-build-sheet.png`, `01-build-rising.png`, `06-at-lock.png`, `06-at-lock-sheet.png`.

## What this changes elsewhere

- **Design/:** `UI/DESIGN-GUIDELINES.md` §Palette (five new tokens) and §Components (Button, and the
  state table); `UI/SCREENS.md` 01 (the digest's button states). Done in this commit.
- **Code:** `LockstepClient/DesignTokens.h` (`BUTTON_PRIMARY_HOVER`, `BUTTON_SEGMENT_SHADE`,
  `MOVE_MODE_WASH`, `INERT_BORDER`, `OUTLINE_HOVER`, `COMMITTED_HOVER_FILL`);
  `LockstepClient/MainPage.{h,cpp}` (`ControlState`, `ControlKind`, `ControlInk`, `ControlInkFor`,
  `DrawControlBox`, `Button`, `ButtonWidth`, `DrawButton`, the button constants, the hover regions,
  `TargetOf`); `LockstepClient/MatchState.h` (`EventAction::number`);
  `LockstepClient/DigestView.cpp` and `Lockstep/SnapshotView.cpp` (the number, composed).
- **Tests:** `LockstepTests/ContrastTests.cpp` (the new grounds), `TouchTargetTests.cpp` (the
  28-drawn/44-tapped claim), `TapTests.cpp` (the two fixture repairs above).
- **AGENTS.md:** nothing.

## Open questions

**Whether the tile, the sheet row and the rail row should draw their state through this routine's
CHROME as well as its table.** They are migrated to the table by ADR-112 and ADR-113, which is where
the shapes differ; what is deliberately not settled here is whether a tile's seven states
(ADR-107) collapse to four or stay seven with four treatments.

**Whether `MOVE_MODE_WASH` belongs in this ADR at all.** It is declared here because the token list
is one list and the four new tokens arrived together; it is used by ADR-114 and by nothing else.

**Whether a locked control needs the square at all.** The sheet's header already wears a `LOCKED`
chip and the rail wears one, so the glyph on each button is the third time the same screen says the
same thing. It is drawn because the handoff asks for it and because a button lifted out of that
context — in a capture, in a bug report — carries no other signal.
