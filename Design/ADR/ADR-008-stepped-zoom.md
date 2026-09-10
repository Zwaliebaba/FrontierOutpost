# ADR-008 — Zoom is a list of even levels, driven by the wheel and by pinch

**Status:** Superseded by ADR-013

**Date:** 2026-09-10
**Decided by:** Build session, at the owner's request for pinch zoom.
**Supersedes:** — (refines ADR-003; see *What this changes elsewhere*)

---

## Context

ADR-003 fixed the camera's scale at 8 virtual pixels per ground unit and wrote the projection as
whole numbers of pixels, because that is what makes the un-projection exact and keeps the 2:1
ground tile a clean two-across-one-down staircase. It also anticipated this ADR, under *What this
makes hard*:

> Zoom is not free. Changing `HALF_TILE_WIDTH_PIXELS` to anything that is not an even number
> breaks the 2:1 into half-pixel steps, so the zoom levels this camera can have are 2, 4, 6, 8 …
> and not a continuous range.

The owner asked for pinch zoom, with the mouse wheel as the fallback for a machine without touch —
which is the same shape MVP-01 §2 already gives the rest of the input: touch primary, mouse the
fallback, no keyboard.

Two facts about Windows constrain the input half. There is **no single zoom event**: the wheel is
one message carrying a signed delta, and a pinch is two contacts whose separation the application
has to track itself across three messages. And the alternative APIs that do unify them —
`WM_GESTURE` with `GID_ZOOM`, or Direct Manipulation — would each mean giving up or rewriting the
pointer path that MVP-01 step 6 already built and tested.

## Options considered

### A. Continuous zoom

Let the scale be any positive number and let a pinch drive it smoothly. This is what a pinch
gesture naturally wants, and it is what every photo viewer does.

It gives up the thing ADR-003 is built on. At a scale of, say, 9.4 pixels a unit, the half-tile
height is 4.7 and a tile edge lands on half-pixels; the staircase becomes irregular, and the
un-projection stops being exactly invertible in floats. The tests that currently compare with
exact equality would have to become tolerance comparisons, which is the same as saying the
guarantee is gone. For a game whose entire premise is a crisp legacy screen, that is the wrong
trade.

### B. A list of even levels, stepped

The scale moves through `{4, 6, 8, 12, 16}` pixels a unit. Every level is even, so the half-height
is a whole number and everything ADR-003 established holds at every level rather than only at the
default.

The cost is that a pinch is quantized: fingers move smoothly and the picture jumps between levels.
On a five-level range with a 1.25× ratio per step, that reads as a chunky zoom rather than a
broken one — and it is the same feel as the tile-based strategy games this projection comes from.

### C. Keep it fixed, and decline

Worth stating because it was a real option: the MVP does not need zoom. Rejected because the owner
asked for it and because ADR-003 had already worked out what it would cost, so the decision was
half-made.

## Decision

**B.** `IsometricCamera::ZOOM_LEVELS_PIXELS` is `{4, 6, 8, 12, 16}` and `ZoomBy(steps)` moves
through it, clamped at both ends. The default stays 8 — the scale ADR-003 records and everything
in the game was drawn against.

Every level is even, and a test asserts it. `ProjectAndUnprojectAreInversesAtEveryZoom` and
`TheTileStaysTwoToOneAtEveryZoom` check at every level, with exact float equality, that the two
properties ADR-003 rests on survive.

**Zoom has no anchor argument.** Zooming normally keeps the point under the cursor fixed; this
camera always centres on whatever it follows (MVP-01 §2 — the ship stays centred and space scrolls
under it), so the anchor is the centre of the screen by construction and there is nothing to pass.
That removes the fiddliest part of both input paths.

**Zooming re-snaps the camera.** The snap to whole pixels is in pixels, and the pixels just changed
size, so `ZoomBy` recomputes it exactly as `Follow` does.

**Two producers, one intent.** `PointerInput` turns both a wheel notch and a pinch into whole steps
on `TakeZoomSteps()`, so the camera never learns which the player used. The wheel banks its
sub-notch remainder, so a high-resolution wheel adds up to notches instead of doing nothing. A
pinch banks a step each time the separation between two contacts changes by 1.25×, re-baselining
each time, and loops so that one large jump banks several steps rather than lagging the fingers.

**The second finger cancels the pending tap.** The first finger of a pinch is indistinguishable
from a tap until the second lands, so without this every pinch would also order the ship to
wherever that first finger happened to touch down.

**Both `WM_POINTERWHEEL` and `WM_MOUSEWHEEL` are handled.** This is the one place this ADR hedges,
and it is deliberate rather than lazy. `EnableMouseInPointer` is documented to route mouse input
into the pointer family, which should make a wheel notch arrive as `WM_POINTERWHEEL` — but that is
the one part of the claim this project has not confirmed against real hardware (see below), and a
handler for only the first would leave zoom quietly not working at all if the claim is wrong. Both
carry the delta in the high word of `wParam`, so the two cases are one line each. This is **not**
the same as the `WM_LBUTTONDOWN` fallback MVP-01 §2 rules out: there the pointer path is proven, so
a second one would be dead code.

## Consequences

**What this makes easy.** Everything ADR-003 guaranteed still holds, at five scales instead of one,
and the tests say so at every level rather than at the default.

**What this makes hard.** A smooth pinch is not available and would be a new ADR reversing this
one. Zoom levels are a closed list; adding one means adding an even number to it and checking
nothing in the scene was sized against the old range.

**What it costs.** The scale is no longer a compile-time constant, so `HalfTileWidthPixels()` is a
call rather than a constant fold, three times per projection. At a couple of meshes a frame that is
not measurable, and it was not measured.

**What is left unverified.** A real pinch and a real wheel have both gone untested on hardware.
This machine has no touch digitizer, and the workstation was locked for the session, so injected
pointer messages are dropped by the window manager — the same wall MVP-01 step 6's physical click
hit. What *was* verified: the whole chain from message to picture, by posting `WM_MOUSEWHEEL`,
which is a classic message and can be injected. Measured on 2026-09-10, driving the wheel down and
back up through the range: the lit area went 3,240 → 6,405 → 10,906 → 16,150 physical pixels as the
scale went 4 → 6 → 8 → 12, and a further notch past the top of the list changed nothing, which is
the clamp. Every level still passed both frame checks: every pixel one of the EGA 16, and every 2×2
block uniform.

## What this changes elsewhere

- **Code:** `NeuronClient/IsometricCamera.{h,cpp}` holds the levels and `ZoomBy`.
  `NeuronClient/PointerInput.{h,cpp}` holds both producers. `FrontierOutpost.cpp` applies the zoom
  before it places the camera, so the click un-projected in the same frame uses the scale the
  player just asked for.
- **Design/:** this **refines ADR-003 rather than superseding it**, and the distinction is worth
  stating. Everything ADR-003 decided — the 2:1 projection, the whole-pixel formulation, the
  snapping, the depth range, the (1,1,1) viewing ray — is unchanged and still in force. The single
  sentence it revises is that the scale is fixed at 8; that becomes "8 by default, one of five
  even levels". `Design/README.md` §4 makes an Accepted ADR immutable, so ADR-003 is left alone and
  this is where the change lives.
- **AGENTS.md:** no change.

## Open questions

Whether five levels are the right five. 4 to 16 is a 4:1 range, chosen so the widest shows the
station comfortably and the closest fills the screen with the ship; nobody has played with it.

Whether the pinch ratio of 1.25 is right, which is the same question and cannot be answered without
a touch screen.

Whether zoom should be replicated. It is a client-side view setting today and the server neither
knows nor cares, which is correct while the camera follows one ship — and would need thinking about
the day a player can have two windows on the same session.
