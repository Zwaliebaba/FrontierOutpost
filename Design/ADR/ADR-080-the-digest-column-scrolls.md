# ADR-080 - The digest column scrolls, and its band says what is below it

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 1.2 at the owner's instruction to work the plan.
**Supersedes:** - (amends ADR-052's option C for one column, and ADR-061's paging)

---

## Context

ADR-052 chose option C -- **nothing scrolls** -- and ADR-061 made a digest that does not fit collapse
and then page, with a 22px band at the foot of the column reading `1 / 4 · MORE ›`.

That holds up badly at the sizes a real match produces. `Design/UI/screens/01-main-page.png` is tick
11 of a six-seat match: **35 events, eight cards visible, twenty-seven behind the band.** The band
says `1 / 4`, which answers "how much column is left" and answers nothing about the question the
player actually has, which is whether the thing that cost them something is down there. The leading
card on that capture is `BATTLE AT ULME / 7 of 10 lost (defending)`; had it fallen on page three,
nothing on the screen would have said so.

Two inputs were already produced and read by nobody. `PointerInput` has banked wheel notches and
pinch steps into `TakeZoomSteps` since ADR-009, and nothing has ever called it. `KeyboardInput`
reaches the join screen and the seats screen and is not drained at all in the match loop, so every
key pressed on the main page is banked and dropped.

## Options considered

### A. Keep paging and fix only the band

Say `27 MORE · 1 BATTLE ›` and leave the rest alone. It is the smallest change and it answers the
question that was being asked. What it does not fix is that reading twenty-seven cards is
twenty-seven-cards-worth of tapping a 22px target at the bottom of the screen, one page at a time,
with no way back but the other half of the same band.

### B. Scroll the column freely, in pixels

What every other list on every other platform does. It costs the thing ADR-052 bought by refusing
it: a card half off the top of the column is a card whose title you cannot read and whose buttons
you can half press, and this screen is a stack of cards with controls in them.

### C. Scroll by whole cards, keep the band

The column's top is a card index rather than a page index. A wheel notch, a drag, or a page key
moves it; the band moves it by a screenful and says what is below. Nothing is ever drawn part-way
off the top, because the top is always a card's top.

## Decision

**C.** The digest column scrolls, by whole cards, and `m_digestTop` -- the index of the card at the
top -- replaces `m_digestPage`. A page stops being a thing that is computed in advance: the frame
draws from the top until the room runs out, and **a screenful is whatever that turned out to be**,
which is what a page key and the band then move by. Cards are different heights, so a page was never
a fixed number and `PreviousDigestTop` measures backwards the same way the draw measures forwards.

**The band says what is hidden and what the worst of it is** (`DigestView::HiddenSummary`):
`27 MORE · 1 BATTLE ›`, or `27 MORE · 2 SYSTEMS LOST ›`, or `END` in dim ink when the bottom is on
the screen. A battle outranks its own kind -- it is a `Loss` card like a system lost is, and the
verdict is what tells them apart, which is the same thing `CanMerge` uses to refuse to fold one.
Composing the sentence is `DigestView`'s job and not the page's, because what a card IS belongs to
the digest (ADR-045).

**Three inputs, one meaning.** A wheel notch away from the player, a drag down, and `PageDown` all
move down the column. The wheel arrives through `TakeZoomSteps`, which banks a count and not a
place, so **the pane under the pointer is what decides what a notch means** -- over the digest it
scrolls, and anywhere else this ADR leaves it alone. A drag banks its remainder toward the next whole
card against `SHEET_ROW_HEIGHT`, the frame's row unit, because a finger moves in pixels and the
column moves in cards; without the remainder a slow drag scrolls nothing at all, which is the same
bargain `PointerInput` already makes with a high-resolution wheel.

**The scroll resets when a new digest arrives**, exactly as the page did: a digest is replaced
wholesale and card thirty of the last one is nowhere in this one.

**The consequence order is what puts the worst thing at the top in the first place**, and the band
is the second half of that answer rather than a substitute for it. `ConsequenceRank` already sorts a
loss and a contact ahead of a rival grouped for offers; that was true before this ADR and is now
load-bearing, so it is pinned by a test rather than left to be noticed.

## Consequences

- **ADR-052 option C is no longer true of the whole screen.** It still holds for the sheets and for
  the locks rail, which are the two surfaces it was really about -- a sheet is six rows by
  construction and a rail that scrolled could hide an order. The digest is the one column whose
  content is unbounded, and it is now the one that scrolls.
- **The leading card is no longer always on the screen**, so the standing moves (ADR-056) can be
  scrolled off. They come back with one notch, and they could not be reached at all from page four
  before this, so the direction of travel is right; but ADR-056's "page one always carries them" is
  now "the top of the column carries them".
- **The keyboard is live on the main page for the first time.** Only `PageUp` and `PageDown` do
  anything, and `HandleKey` returns false for everything else so the loop costs no frame for a key
  this screen does not use. The window procedure already forwarded them; nothing drained them.
- A player on a touch device gets the drag and the band; a player with a mouse gets the wheel as
  well. Which of those this game is actually for is UI-01 item 3.8 and is still open.
- `m_cardsOnScreen` is a layout answer read a frame late, like the hit list. That is the same
  bargain this screen already makes everywhere and for the same reason.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01 (the overflow paragraph and the band), `DESIGN-GUIDELINES.md`
  (the page band component). ADR-052's option C is amended by this file rather than edited, and
  ADR-061 keeps its collapse. Done in this commit.
- **Code:** `NeuronClient/KeyboardInput.{h,cpp}` (`PageUp`, `PageDown`),
  `LockstepClient/DigestView.{h,cpp}` (`HiddenSummary`), `LockstepClient/MainPage.{h,cpp}`
  (`m_digestTop`, `m_cardsOnScreen`, `m_digestDragPixels`, `HandleZoom`, `HandleKey`,
  `ScrollDigest`, `PreviousDigestTop`, the draw and the band), `Lockstep/Lockstep.cpp` (the match
  loop drains the wheel and the keys). Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `DigestOverflowTapTests` gains four -- a notch over the digest scrolls one card and
the same notch over the map scrolls nothing; the page keys move by the screenful the frame measured
rather than by a card; a drag banks its remainder and moves a card at 44 pixels and not at 10; and
the band reports the hidden count and says nothing when the bottom is on screen. Its existing
`ADigestTallerThanTheColumnPages` is kept and now sweeps for the top rather than the page.
`DigestViewTests` gains `AConsequenceOutranksAGroupedRivalWithoutOne`.

**Not photographed.** The desktop was locked for this session, so `01-main-page.png` has not been
retaken and the band has not been seen on a real 35-event digest; the tests drive the real draw and
the real hit list, which is what says the band is there and what it says.

## Open questions

**What a wheel notch over the MAP means.** UI-01 item 2.5 wants it to zoom the camera, which is what
`TakeZoomSteps` was banked for in the first place. `HandleZoom` is shaped for that -- it takes the
position and returns whether anything moved -- and does nothing with it today.

**Whether the band should name what is ABOVE as well.** `‹ PREV` is bare where the right-hand half
now carries a summary. It reads fine because what is above is what the player has already read, but
that stops being true the moment a new digest arrives while they have scrolled.
