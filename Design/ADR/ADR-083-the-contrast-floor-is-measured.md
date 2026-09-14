# ADR-083 - The contrast floor is measured, and one palette owns the numbers

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 2.2 at the owner's instruction to work the plan.
**Supersedes:** - (closes ADR-045's open item on the private palette copies)

---

## Context

Every text colour on these screens is a light grey at an alpha over a dark ground. That is the one
place a palette drifts below legible without anyone noticing: the ink still looks like the ink, it
is just fainter, and the person who picked the alpha is the person least able to see it.

`DesignTokens.h` is the one list -- ADR-045 collected it when the map moved out of `MainPage` -- but
three files never joined it. `SeatsPage.cpp`, `JoinPage.cpp` and `ConnectionDialog.cpp` each carried
their own literal copy of the same dozen colours, thirteen, ten and nine entries respectively, and
ADR-045 left folding them in as an open item. All three also carried their own `CenterTextY`,
identical to the header's and to each other's.

UI-01 2.2 estimated `TEXT_MUTED` at about 4.0:1 and `NEUTRAL_DIM` at about 3.1:1 over the
background, and asked for both to be raised.

## The measurement

**Those estimates were wrong, and the direction matters.** Measured on 2026-09-13 by
`ContrastTests`, which is WCAG 2.1's ratio over the composited pixel:

| Token | Alpha | Over `APP_BACKGROUND` |
|---|---|---|
| `TEXT_MUTED` | 140 | **4.80:1** -- above the floor |
| `TEXT_DETAIL` | 153 | above the floor |
| `NEUTRAL_DIM` | 115 | **3.61:1** -- below it |

So one token was actually failing, not three, and the one that was failing was worse than estimated
only by a little. This is `Design/README.md` §3.2 earning itself: an unsourced number becomes a
requirement within two documents, and this one would have become "we raised three tokens because
they failed" when two of them did not.

## Options considered

### A. Raise only `NEUTRAL_DIM`, to the smallest value that clears the floor

The minimum change the measurement justifies. It leaves `TEXT_MUTED` and `TEXT_DETAIL` sitting just
above 4.5:1, where the next change to the background gradient or the next font cut puts them under
again with nothing to say so.

### B. Raise all three to UI-01's values, and assert the floor from then on

What the plan recorded. The two that pass gain margin they did not strictly need; the assertion is
what stops the next drift rather than the margin.

## Decision

**B, with the measurement recorded rather than the estimate.** `TEXT_MUTED` 140 -> 168,
`TEXT_DETAIL` 153 -> 168, `NEUTRAL_DIM` 115 -> 140. `TEXT_DETAIL` and `TEXT_MUTED` are now the same
byte and keep both names: they mean different things -- a sentence under a title, and a label beside
one -- and the day either moves it will move alone.

**`ContrastTests` is the floor, and it is a test rather than a script.** UI-01 offered either; a test
runs in CI with the other four suites and a `Build/Contrast.ps1` would have to be remembered. It
holds every text token and every meaning colour to 4.5:1 over both grounds this game paints text on
-- `APP_BACKGROUND` and the dialog card -- and it asserts that `NEUTRAL_DIM` stays dimmer than
`TEXT_MUTED`, so the floor cannot be cleared by flattening the palette to white.

**The dialog card becomes `Ink::DIALOG_FILL`.** It is the one ground that is not the app background:
opaque where every other card is a wash, because it sits over a scrim and over the board.

**The three private palettes become local names bound to the one list.** `constexpr Color TEXT_MUTED
= Ink::TEXT_MUTED;` and so on: the names stay local because each is used dozens of times in its file
and `Ink::` at every site is noise, and what moved is where the VALUE comes from -- the half that
could ever be wrong. The three copies of `CenterTextY` are deleted outright; the header's is
identical and now reachable.

**The inert-button bullet was already true.** UI-01 asked that an at-lock or unaffordable button
carry its state in the frame as well as the alpha; `MainPage` already strokes it in `Ink::OUTLINE`
and writes it in `NEUTRAL_DIM`. Recorded here so the next reader does not go looking for the change.

## Consequences

- **Three screens got measurably lighter text** and the captures of them are stale. The at-lock
  state in particular is drawn almost entirely in the two tokens that moved.
- **`TEXT_DETAIL` and `TEXT_MUTED` are indistinguishable on screen** until one of them moves. A
  hierarchy that was three greys is now two, which is a real loss of a level and is the price of the
  floor at this font size. UI-01 2.1's second type size is where that level comes back.
- **`Lockstep/SeatsPage.cpp` now includes a `LockstepClient` header.** It is a bridge file compiled
  into both the executable and `LockstepTests` and already reaches across; the include path carries
  it (AGENTS.md §2), and nothing about the `GameLogic` edge changes.
- A star is in the token list now and is deliberately NOT held to the floor: a legible star is a
  defect.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Palette (the three alphas, the new tokens, and that the floor
  is asserted). Done in this commit.
- **Code:** `LockstepClient/DesignTokens.h` (alphas, `DIALOG_FILL`, `STAR`),
  `Lockstep/SeatsPage.cpp`, `LockstepClient/JoinPage.cpp`, `LockstepClient/ConnectionDialog.cpp`
  (include the header, bind the names, drop the duplicate helper),
  `Tests/LockstepTests/ContrastTests.cpp` (new, registered in the `.vcxproj` and `.filters`).
  Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `ContrastTests` -- three methods over eight tokens and two grounds. Run against the
palette as it was before this change, it failed on `NEUTRAL_DIM` at 3.608321:1, which is the
measurement in the table above; it passes on the palette as it is now. All 133 methods in the suite
pass.

**Not photographed** -- the desktop was locked for this session, so `06-at-lock.png`,
`06-at-lock-sheet.png` and `01-finished.png` are stale.

## Open questions

**Whether `TEXT_DETAIL` should differ from `TEXT_MUTED` again.** They are one byte apart in meaning
and zero apart in value, which is a hierarchy level the screen has lost. The answer is probably size
rather than alpha (UI-01 2.1), and until that lands there is nothing to spend here that does not
push one of them back toward the floor.
