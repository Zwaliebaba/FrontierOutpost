# ADR-101 - The locks rail scrolls, and its page band is the control

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Follows from ADR-100: 44-pixel rows in a column sized for 21-pixel ones. Answers ADR-086's open question, *"what the rail does when the sections do not fit"*.
**Supersedes:** -

---

## Context

The locks rail has never had an answer for running out of room. It did not need one while a row was
21 pixels: four sections and their contents fitted in the 558 pixels between the help line and the
footer for any empire this game had been played with.

ADR-100 made a row 44. The same four sections now need roughly twice the column, and a played
empire -- several fleets standing at several systems, two or three builds queued, the signals going
out this tick, an open proposal -- runs off the bottom. What ran off did not merely go unread: it
was **drawn below the footer**, over `ALL LOCK TOGETHER`, and its hit rectangles were still there to
be tapped.

The digest column opposite solved this three ADRs ago (ADR-080): it scrolls, and it has a page band
at its foot that says what is hidden.

Two things about this column are not the digest's, and they are what the decision is about.

**It is not a stack of cards.** The digest pages by card, because stopping part-way through a card
puts its title off the top. The rail is a list of fixed 44-pixel rows and the bands under section
headers, so every pixel offset lands somewhere legible.

**`ShapeRenderer` has no clip rectangle.** `FontRenderer` has one; the shapes -- a row's divider,
its hover fill -- do not. A half-scrolled row would paint over the help line above it however
carefully its text was clipped.

## Options considered

### A. Let it clip and say nothing

What it did. Rows below the fold are drawn over the footer and are tappable there.

### B. Scroll on the wheel, mark the edges with chevrons

A wheel notch over the rail moves it by a row, and a muted `‹` / `›` appears at whichever edge has
something past it. Small, and it was built first.

**It is wrong twice.** `‹` and `›` mean something already in this tree -- they are the digest
pager's matched horizontal pair and the trailing "go" affordance on `JOIN ›` and `1 TO SEND ›`. A
`›` at the bottom of a column would say "go" where it meant "below". And a wheel is not a finger:
**this game is for touch** (ADR-098), so a column whose only scroll affordance is a mouse gesture is
a column whose bottom half this game's players cannot reach at all.

### C. The digest's page band, on the rail

A band at the foot of the column: a divider, `‹ UP` on the left once there is something above, and
what is below on the right. Both halves are 44-pixel targets. The wheel still works and is the
shortcut rather than the only way.

## Decision

**C.** The rail scrolls, and **the band is the control while the wheel is the shortcut.**

**It says what is below, not only that something is.** That is ADR-080's whole point, and the rail's
version of it is the next section's name: `3 MORE · SIGNALS ›` tells a player whether the thing they
are looking for is down there. `END` in the dim ink when it is the bottom, exactly as the digest
does.

**What is hidden is counted while drawing, not predicted.** A row's height depends on how its title
wrapped, so the cull is the only thing that knows what did not fit.

**A row that is not wholly inside the band is not drawn at all**, which is coarser than clipping and
is what the renderers can actually do. The scroll step is one row, so in the ordinary case nothing
is ever half anything; the cull is what keeps that true when a wrapped row is taller than the step.

**A culled row registers no hit.** Culling the drawing and keeping the hit would leave an invisible
control over the help line, which is worse than either half of the bug.

**The band's own reservation is a frame late, and it cannot oscillate.** Whether the rail overflows
depends on how tall the band is, and how tall the band is depends on whether it overflows. The loop
is broken by reserving from the previous frame's answer -- the same one-frame-late discipline the
hit list already runs on, for the same reason. Reserving the band only ever makes the column
shorter, so a rail that was paged stays paged and a rail that was not becomes paged at most once:
there is no content height that flickers between the two.

**The scroll survives the tick.** `m_railScrollPixels` is not reset by `Create`, unlike
`m_digestTop`. A digest is replaced wholesale and card thirty of the last one is nowhere in this
one; the rail is a summary of the same empire tick after tick, so a player who has scrolled to their
signals expects to still be looking at them when the tick lands.

## Consequences

- **The header and the footer do not move.** Which tick is locking, and that all three columns go in
  together, are true however far down the list you are.
- **The band costs 44 pixels of column when it is there**, which makes a rail that was one row from
  overflowing overflow. That is the hysteresis above, and it is the right direction: the column that
  needs the control gets it.
- **A tap on the band moves by a bandful less one row**, so the row a player was reading is still on
  the screen afterwards. The digest pages the same way.
- **The rail now has a scroll position that can be wrong after a tick** -- if the sections shrink,
  the clamp pulls it back on the next draw, which is one frame of being scrolled past the end.
- ADR-086's open question is closed.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` (screen 01's rail), `DESIGN-GUIDELINES.md` §Columns,
  `Design/Plans/UI-02-TouchTargets.md`.
- **Code:** `LockstepClient/MainPage.{h,cpp}` -- `Action::PageRail`, `RAIL_PAGE_HEIGHT`,
  `ScrollRail`, `m_railScrollPixels` / `m_railContentPixels` / `m_railViewportPixels` /
  `m_railPaged`, the rail case in `HandleZoom`, and the cull in the rail's draw.
- **Tests:** `Tests/LockstepTests/TapTests.cpp` `RailScrollTests`.
- **AGENTS.md:** nothing.

## Verification

`RailScrollTests` over a rail with ten fleets across five systems:

- a wheel notch scrolls it and it stops at both ends, reporting no change when it is already there;
- a rail that fits does not scroll at all and draws no band;
- **every hit stays inside the band at every scroll position** -- above the help line and above the
  footer -- which is the assertion that caught the `SIGNALS` header's hit escaping, since that one
  is registered outside the section lambda and had to be culled explicitly;
- the band's two halves scroll the column by tap and return it exactly to its top.

## Open questions

**A drag, rather than a wheel and a band.** A finger on a list scrolls it by dragging, and this
column does not. `PointerInput` already distinguishes a drag from a tap by slop (ADR-016) and the
digest already carries a `m_digestDragPixels`, so the machinery is half there. Left out here
because a drag that starts on a row is a drag that must not fire that row's tap, and deciding that
is its own change.

**The band names one section.** `3 MORE · SIGNALS ›` names the first section header below the fold,
which is the wrong thing to name when what is below is three more fleets and no new section -- then
it says `3 MORE ›`. Whether the more useful answer is "3 MORE FLEETS" is a question for somebody
watching a player look for something.
