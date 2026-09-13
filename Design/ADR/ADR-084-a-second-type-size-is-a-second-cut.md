# ADR-084 - A second type size is a second baked cut

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 2.1 at the owner's instruction to work the plan. Answers the open question ADR-074 left about the countdown.
**Supersedes:** - (amends ADR-074's cut list and ADR-014's one-size rule)

---

## Context

Every string on every screen is 12px. Hierarchy is carried by weight (one step, Regular against
Medium), by case, and by colour -- and ADR-083 has just spent one of those, folding `TEXT_DETAIL`
and `TEXT_MUTED` onto the same byte to clear the contrast floor. A card's title and the sentence
under it are now separated by a weight step and nothing else.

The one exception was worse than the rule. The lock countdown and the `LOCKSTEP` titles were drawn
at `COUNTDOWN_SCALE` -- a whole-number pixel-block enlargement of the 12px face. That was exactly
right when the font was a hand-typed 8x8 grid, where doubling WAS the second size and every edge was
already a hard pixel. Beside anti-aliased Plex it is a different thing: the doubled glyph has
doubled anti-aliasing, so its edges are two-pixel ramps where every other string's are one.
**ADR-074 left this open in as many words** -- whether the countdown should become a larger baked
cut instead -- and noted it was the only way left to make it heavier once Plex Mono SemiBold was
dropped.

## Options considered

### A. A size argument on the renderer, and rasterize at request time

What "expose a size selector" sounds like. It is not available: the atlas is baked offline into a
committed header (ADR-073) and the binary ships alone (R13), so there is no rasterizer at runtime to
select a size from. A size that is not baked cannot be drawn.

### B. Keep 2x and live with it

Free. It is the artefact above, and it is the thing ADR-074 asked about.

### C. Bake a second size as a fifth cut

`Face` already means "which type", and in a baked-atlas renderer a cut IS a size -- the four existing
cuts differ by weight and family only because that is what has been baked so far. A 16px cut is a
fifth entry in the same table, reached by the same `Face` argument every call site already takes.

## Decision

**C.** `Face::MonoDisplay` is IBM Plex Mono Medium at 16px, hinted, baked by the same
`Build/BakeFont.py` run as the other four. **`COUNTDOWN_SCALE` is deleted** and nothing in the game
draws at a scale other than 1; the scale argument stays on the renderer because a whole-number
blow-up is exact and free, not because anything asks for one.

**Six things are set in it and nothing else**: the lock countdown, a digest card's title, the digest
header, a sheet's header, a dialog's title, and `LOCKSTEP` on the join and seats screens. The rule
is that each of them names what a whole pane, card or screen IS -- which is the level of hierarchy
the screen had no way to express.

**It is mono, and the face rule holds it to that.** `FaceRuleTests::IsMono` names the family rather
than listing two weights, so the display cut carries data and is held to ADR-074's mono half like
the others. A list written as "the two mono faces" is a list that goes wrong the next time a cut is
baked, which is this time.

**Line boxes are asked of the font, never stated.** `MainPage::TITLE_LINE_HEIGHT` is
`LineHeightPixels(Face::MonoDisplay)` and a card's title block is laid out from it; the sheet header
centres with `CenterTextY(..., Face::MonoDisplay)`. That is the same discipline `LINE_HEIGHT`
already enforces, applied to the second size -- and the reason it exists is that a title in a bigger
cut over a line box sized for the smaller one is precisely the overlap that number was introduced to
stop.

## The measurement

From the baked header, 2026-09-13:

| | Body cut (12px) | Display cut (16px) |
|---|---|---|
| Advance | 7px | **10px** (0.600em at 16px is 9.6) |
| Line box | ascent 13 + descent 4 = 17 | ascent 17 + descent 5 = **22** |
| Face line height | 16, floored to 17 | 21, floored to **22** |
| `02:14:09` | 56px | **80px** (the old 2x was 112px) |

**The countdown got narrower**, by 32 pixels, which is space back on the top bar rather than a cost.

## Consequences

- **`Font.h` grew by about 40KB** -- 505 glyphs against 404 -- and the binary with it. It is one
  atlas and one table; the header is committed and hashed (ADR-073), so the growth is visible in the
  diff rather than in a build step.
- **Every one of the 23 captures in `Design/UI/screens/` is stale**, because the face changed on
  every screen. None were retaken: the desktop was locked for this session.
- **The card title block is five pixels taller**, so a digest column fits marginally fewer cards.
  ADR-080 made that a scroll rather than a cliff, which is the order those two items were done in
  and the reason it is not a problem.
- **Emphasis is now colour, case, weight and size.** That is one more axis than the screen had and
  one more than ADR-014 allowed for; the cost is that a fifth cut is a fifth thing to keep in step
  when the type changes again.
- ADR-074's Decision still says five cuts and now means a different five: SemiBold out, Display in.
  Amending its text is still the owner's call (`DESIGN-GUIDELINES.md` §Font says so already).

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Font -- the cut list, the display cut's measurements, and the
  2x bullets deleted. Done in this commit.
- **Code:** `Build/BakeFont.py` (the `FACES` table), `NeuronClient/Font.h` (re-baked, generated),
  `NeuronClient/FontRenderer.h` (`COUNTDOWN_SCALE` gone), `LockstepClient/MainPage.{h,cpp}`
  (`TITLE_LINE_HEIGHT`, the countdown, the card title, the digest header, the sheet header),
  `LockstepClient/JoinPage.cpp`, `LockstepClient/ConnectionDialog.cpp`, `Lockstep/SeatsPage.cpp`,
  `Tests/NeuronClientTests`, `Tests/LockstepTests/FaceRuleTests.cpp`. Done, built and photographed.
- **AGENTS.md:** nothing.

## Verification

`NeuronClientTests`: `TheDisplayCutIsASizeAndNotAScale` pins the advance at 10, the box at 22, that
it is bigger than the body cut, and that it is still fixed-pitch. `MeasuringSumsTheAdvances` adds
the 80px countdown. `LockstepTests`: `FaceRuleTests` passes with the display cut counted as mono --
it failed first with `DIGEST - TICK 12` and a card title reported as shouted sans, which is the
check doing its job. All 233 methods across the two suites pass.

Photographed on 2026-09-13 against a `--serve --phase0 --bots 5` server with a `--join` client, at
T4 and at the finished match: the countdown, `DIGEST - TICK 4`, the card title `PRODUCTION +6` over
its 12px sans detail line, and the `MATCH FINISHED` dialog title are all in the display cut, and no
title overlaps the line under it.

## Open questions

**Whether the digest's section headers and the locks rail's should follow.** They are the same kind
of string -- a label naming what is under it -- and they were left at 12px because six strings is a
change a reader can hold and sixteen is a redesign. A screenshot decides it and this session could
not take a comparative one.

**Whether Plex Sans wants a display cut too.** Nothing is set in one today; the naming strings are
all mono. If a sans title ever appears, baking a fifth-and-sixth pair is the same one-line change to
`FACES`.
