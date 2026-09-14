# ADR-096 - A lobby control says what it will do, and a disabled one can still be read

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 3.3 at the owner's instruction to work the plan.
**Supersedes:** -

---

## Context

Two controls in the seats screen's footer.

**`FILL WAITING WITH BOTS` does not say which seats.** It turns every seat still marked
`WAITING FOR PLAYER` into a bot -- which is the host deciding that the people who have not arrived
are not coming. That is a real decision and the label describes half of it.

**`ENTER MATCH ›` when disabled was `NEUTRAL_DIM` text inside a `DIVIDER` border** -- 0.45 alpha
inside 0.07 -- so a host who could not enter had to work out both THAT it was disabled and WHY from
the same nearly invisible thing.

## Decision

**A muted sentence under the footer's summary: `Sets every WAITING FOR PLAYER seat to BOT.`**

**Drawn always, not on hover.** UI-01 asked for hover or selection. `SeatsPage` tracks no pointer,
and adding one is real plumbing for a hint -- but the deciding argument is that **UI-01 3.8's answer
is that this game is for touch** (ADR-098), and on a touch device a hover hint is a hint nobody ever
sees. A sentence that is always there costs one muted line and works for everybody.

**The disabled `ENTER MATCH` is `TEXT_MUTED` in an `OUTLINE` border.** It says "not now" by being
outlined rather than filled; the footer's own sentence -- `WAITING FOR SORNE, OKONKWO, +2` -- is the
reason, and it is already there, in colour, a few pixels to the left. A control has to be readable to
say anything at all, including that it is off.

## Consequences

- **The footer is two lines rather than one.** It was a 44px band with one centred line and now has
  a summary and a sentence under it, which is what the band's height was already paying for.
- **The disabled button is more visible than it was**, which is the point and is also a small risk:
  a host may try to press it. It takes no hit when disabled, and the sentence beside it says why.
- One more always-drawn sentence on a screen that is mostly cards. It is the only control on the
  screen whose effect is not in its label.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 09. Done in this commit.
- **Code:** `Lockstep/SeatsPage.cpp`. Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `SeatsPageTapTests` passes unchanged -- it sweeps for the controls rather than for
coordinates, so a footer that grew a line does not move what it finds. All 154 methods pass.

**Not photographed** -- captures are being taken as one pass at the end of UI-01.

## Open questions

**Whether `SeatsPage` should track a pointer at all.** The main page does, for the locks rail's hover
fill (ADR-060). If the answer to 3.8 is ever revisited and this becomes a mouse game, a hover would
be the better home for this sentence.
