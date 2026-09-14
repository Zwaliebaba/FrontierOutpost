# ADR-098 - This game is for touch, and its targets are not

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision, answering `Design/Plans/UI-01-ClientImprovements.md` item 3.8's [ASK]: *"Touch - grow every target."*
**Supersedes:** -

---

## Context

Every player-facing string on this client says **tap**. `PointerInput` was built around the Windows
Pointer API for exactly that reason -- `EnableMouseInPointer` so a mouse and a finger arrive through
one channel, a four-pixel tap slop so a finger that lands and lifts on two pixels is still a tap,
pinch handling for two contacts (ADR-016, ADR-009). `MainPage::SHEET_ROW_HEIGHT` is 44 and its own
comment says *"the smallest target a finger hits reliably"*.

**And almost nothing else on the screen is 44.** Measured from the constants on 2026-09-14:

| Target | Height | Where |
|---|---|---|
| Sheet row | **44** | `SHEET_ROW_HEIGHT` |
| Sheet header / close corner | 36 | `SHEET_HEADER_HEIGHT` |
| Sheet band, digest title, page band | 22 | three constants |
| Top bar chips (`REPLAY`, placement) | 20-22 | `DrawTopBar` |
| Locks rail row | **21** | `LINE_HEIGHT + 4` |
| Digest card button | **18** | `BUTTON_HEIGHT` |
| `RESET` chip | 18 | reuses `BUTTON_HEIGHT` (ADR-090) |
| Garrison badge | **16** | `BADGE_HEIGHT` (ADR-079) |

The three a player uses most -- the digest's buttons, the rail's rows, and now the map's badges --
are the three smallest, at 18, 21 and 16 pixels. **The badge is the newest and the worst**, added in
this very plan.

The 44-pixel sheet row is the outlier that proves the intent: somebody knew the number and applied
it once.

## Options considered

### A. Mouse-only: change the copy

Change "tap" to "click" everywhere and record that this is a desktop game. One afternoon, no layout
change, and it makes 18px correct rather than undersized. It also throws away what
`PointerInput` was built for and closes a door the one-pager never closed.

### B. Touch: grow every target

Buttons and rail rows to at least 32, ideally 44. It is a change to essentially every vertical
constant on the main page, and therefore to every capture and to the wrapping of a digest column
that just learned to scroll.

## Decision

**B, on the owner's answer.** This game is for touch, the copy stays "tap", and **the targets are
wrong and are known to be wrong.**

**The work is not done here.** UI-01 3.8 says so itself -- *"which changes every layout constant --
do it as its own plan"* -- and it is right: a 44-pixel digest button changes how many buttons fit on
a card, which changes what `LayoutCard` measures, which changes how many cards a screenful is
(ADR-080), which changes the band. That is a redesign of the digest column, not an item. It is
`Design/Plans/UI-02-TouchTargets.md`.

**What this ADR fixes now is the record.** `DESIGN-GUIDELINES.md` gains the floor and the table
above, so that the next control anybody adds is measured against 44 rather than against the button
beside it -- which is how 16 happened.

## Consequences

- **The client ships with targets it knows are too small**, on a platform it has no build for. That
  is the honest state: nothing in this tree runs on a touchscreen today, so the defect is latent, and
  writing it down is what stops it being re-litigated every time somebody adds a chip.
- **The 44 floor is now a rule rather than one constant's comment.** A control that cannot be 44 has
  to say why, in the place it is declared.
- **Twelve captures will change** when UI-02 lands, so retaking them now is work that will be
  redone. That is an argument for doing UI-02 before the capture pass, and it is the owner's call.
- The map's garrison badge is the hardest case: it sits beside a system name at a size chosen to fit
  a label's line, and 44 pixels there would be a different design rather than a bigger box.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Frame (the touch floor and the measured table),
  `Design/Plans/UI-02-TouchTargets.md` (new). Done in this commit.
- **Code:** nothing. This decision is ahead of the code, deliberately.
- **AGENTS.md:** nothing.

## Verification

None: no code changed. The table above is read from the constants named in it.

## Open questions

**Whether 32 or 44.** UI-01 says "at least 32"; `SHEET_ROW_HEIGHT`'s own comment says 44 is the
smallest a finger hits reliably. UI-02 has to pick one, and picking 44 may not fit a digest card
that also has to hold two or three buttons at 400px wide -- which is the real reason this is a plan
and not an item.

**What the game is for on a desktop.** Touch targets on a mouse-driven screen are large but not
wrong. If both are wanted, the frame has a scale already (ADR-075) and the question becomes whether
the canvas is authored at one size for both.
