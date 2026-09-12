# ADR-045 — The map moves out of the main page

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, "do 5, 11 and the console", 2026-09-12.

**Closes:** the codebase review's §4 item 5.

---

## Context

The review asked for *"layout separated from drawing in `MainPage`, so taps can be tested and the
file shrinks"*. **Half of that was already done and by a different route**: ADR-041 gave the
renderers a headless path, so a test drives the real draw and presses the real hit list, and
`AddHit` still sits beside the `FillRect` that put the button there. Sixty-four tap tests came from
that, and separating layout from drawing would have cost the invariant they rely on.

What remained was the file: 1,662 lines doing four unrelated jobs — a top bar, a digest rail, a
galaxy, and an orders rail with a modal over it.

## Decision

**The map moves to `MapRender`, whole.** It is the one genuinely separable subject in the file: 450
lines that project through a camera, and the only part that touches `MapView`. `DrawWorld` and
`DrawInterface` were already the seam — the map is the world, the rails are the interface, and they
are flushed as two layers because each renderer is one batch.

**It returns its hits instead of reaching into the page.** A `MapHit` names a system or a fleet and
not an action: the map knows what things are, the page knows what tapping one does. That is what
lets the map leave without taking a reference to `MainPage`, and it keeps the hit beside the drawing
that produced it.

**`MapFrame` is a struct of references and one of them is mutable.** Drawing sets the viewport and
frames the content, because both depend on the pane the map was given and the pane is the page's to
decide. That is the only thing in it that is not read-only, and it is why it is references rather
than a copy.

**`DesignTokens.h` holds the palette, the frame and three helpers.** Extracting the map forced it:
both halves used the same colours, and they could not both keep a private copy. It turned out there
were **five** copies — `MainPage`, `SeatsPage`, `JoinPage`, `ConnectionDialog` and `MapRender` each
carried the same dozen constants, because each was written on its own and none could see the others.
Five copies of a palette is five palettes on the day one of them is adjusted. The name is the design
record's own: `Design/Screens/README.md` calls them design tokens.

The map's own numbers — grid spacing, stem heights, halo scales, the region's reach — are not there.
They are the map's and they live with it.

## What this is verified by

**Two screenshots of a live match, byte-identical.** The same seed, the same bot roster, the same
tick, captured before and after: `72AC3713…` both times. A refactor with no behaviour change is
exactly the kind that goes wrong invisibly, and a hash of the frame is the only assertion that
covers all of it at once.

That check was available because of the harness this session already had: a headless server with
bots, a client joined by command line, and a DPI-aware client-area capture. None of it needed a tap,
which matters because the desktop has been locked throughout.

434 tests pass in both configurations, including the 64 tap tests that drive `DrawWorld` and press
what it produced.

## Consequences

**`MainPage.cpp` is 1,157 lines**, from 1,662. It now does a top bar, two rails and a modal.

**Two more files are registered twice.** `LockstepTests` compiles nine of the executable's
translation units a second time, and every new one has to be added to two projects or the test DLL
stops linking — which is how this change was reminded of it. The review's §4 item 3, a client
library, is the answer and is not taken here.

**The other four pages still carry their own palettes.** `DesignTokens.h` exists and they do not use
it yet; converting them is a mechanical change with a screenshot each, and doing it in the same pass
as a 450-line move would have made the byte-identical comparison above meaningless.

## Open questions

**The digest rail and the locks rail are still 400 lines together in `MainPage`.** They are more
alike than the map was, and the case for splitting them is weaker: they share a section helper, a
row helper and a column. Worth revisiting only if a third rail appears.
