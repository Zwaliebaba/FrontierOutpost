# ADR-008 — Zoom is a list of even levels, driven by the wheel and by pinch, about the point under the pointer

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session on 2026-09-10 decided the levels and the two producers; owner decision, 2026-09-10, adds the anchor for the map camera and rewrites this file in place (`Design/README.md` §4).
**Supersedes:** —

---

## Context

ADR-003 fixes the projection in whole pixels, and its arithmetic only stays exact if the scale
does too. Zoom therefore cannot be continuous: a half-tile width of 8.3 pixels puts every lattice
point off the grid. And the camera no longer follows a ship at the centre of the screen: it pans
over a bounded map (ADR-003), so the point the player is looking at is wherever the pointer is,
not the middle.

Two producers exist. A mouse wheel arrives as `WM_POINTERWHEEL` (ADR-009). A pinch is two contacts
whose separation changes, tracked by `NeuronClient/PointerInput`.

## Options considered

### A. Continuous zoom with the camera re-snapping afterwards

Smooth, and it breaks the ADR-003 property every frame it is between levels: the ground lattice
is off-grid, the ship crawls, and the pinch feels right for the wrong reason.

### B. A short list of even integer half-tile widths

`{4, 6, 8, 12, 16}` pixels. Every level is even, so a tile's height — half its width — is a whole
pixel too, and every ADR-003 test holds at every level. The step between levels is coarse, and
that is visible on a pinch, where the fingers move smoothly and the map moves in jumps.

## Decision

**B.** `IsometricCamera::ZOOM_LEVELS_PIXELS` is `{4, 6, 8, 12, 16}` and `ZoomBy(steps, anchor)`
moves through it, clamped at both ends. The default stays 8, the scale everything in the tree was
drawn against. Every level is even, and a test asserts it; `ProjectAndUnprojectAreInversesAtEveryZoom`
and `TheTileStaysTwoToOneAtEveryZoom` check, with exact float equality, that the two properties
ADR-003 rests on survive at each.

**Zoom has an anchor.** The world point under the anchor pixel before the zoom is under the same
pixel after it, so a player zooming into a system keeps it under the finger. The anchor is the
pointer position for a wheel notch and the midpoint of the two contacts for a pinch. The camera's
target moves to make that true and is then clamped to the map bounds, in that order, so a zoom at
the edge of the map still lands on the map.

**Zooming re-snaps the camera** to whole pixels at the new scale, as `Follow()` does.

**Two producers, one intent.** `PointerInput` turns both a wheel notch and a pinch into whole
steps on `TakeZoomSteps()`, with the anchor beside them, so the camera never learns which the
player used. The wheel banks its sub-notch remainder. A pinch banks a step each time the
separation changes by 1.25×, re-baselining each time, and loops so one large jump banks several
steps.

**The second finger cancels the pending tap.** The first finger of a pinch is indistinguishable
from a tap until the second lands.

## Consequences

**What this makes easy.** ADR-003's exactness at every level, by construction and by test.

**What this makes hard.** A pinch reads as stepped. Accepted: the alternative is a blurred map.

**What it costs.** An anchor argument and the clamp order, which the MVP-01 camera did not need
because the ship was always at the centre.

**What it forecloses.** Zoom levels that are not even, and continuous zoom.

## What this changes elsewhere

- **Design/:** ADR-003 (the panning camera) and ADR-015 (the scene the camera looks at).
- **Code:** `NeuronClient/IsometricCamera.{h,cpp}` and `NeuronClient/PointerInput.{h,cpp}`
  implement the levels and the two producers today. The anchor is not implemented: `ZoomBy` takes
  steps only, because the tree's camera follows the ship. `Design/Plans/MVP-02-TheLoop.md` step 6
  adds it.

## Open questions

Whether a sixth level is wanted for a whole-galaxy overview at twelve seats. Decided by measuring
a generated galaxy at step 6, not here.
