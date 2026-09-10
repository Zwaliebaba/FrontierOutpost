# ADR-003 — The camera is 2:1 dimetric, written in whole pixels, snaps to the pixel grid, and pans over a bounded map

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session MVP-01 decided the projection and the snap on 2026-09-09; owner decision, 2026-09-10, replaces the follow-the-ship camera with a panning map camera and rewrites this file in place (`Design/README.md` §4).
**Supersedes:** —

---

## Context

`Design/README.md` §1 fixes a 640×400 framebuffer scaled by a whole number, and ADR-001 makes
every pass write a palette index. A projection that puts world points between pixels defeats both:
a lattice of world points has to land on a lattice of pixels, or the picture swims.

What the camera looks at has changed. The MVP-01 camera followed one ship through unbounded space
and never needed to be moved by hand. The 4X client draws a bounded galaxy as an isometric
diorama on the ground plane (ADR-015): systems, lanes and fleets at display coordinates the
generator emits (ADR-014). The player moves the camera; the digest and a selection move it too.

## Options considered

### A. A matrix from a yaw and a pitch

The general camera. Its coefficients are sines, so the projection of a lattice is not a lattice,
and the inverse — the pick, which every tap depends on — accumulates error.

### B. An exact 2:1 dimetric projection in whole pixels

```
pixelX = (x - z) * HALF_TILE_WIDTH_PIXELS      // 8 at the default zoom
pixelY = (x + z) * HALF_TILE_HEIGHT_PIXELS     // 4
       - y       * HEIGHT_PIXELS_PER_UNIT      // 8
```

Every coefficient is a whole number of pixels, the inverse is two divisions, and
`NeuronClientTests::IsometricCameraTests` compares with exact float equality. Its null direction
is (1, 1, 1): true isometric with the vertical axis squashed by √3/2, yaw 45°, elevation 30°.

### C. Top-down, no projection at all

Legible and cheap, and it throws away the meshes, the two-tone shading and the reason the game
looks like anything.

## Decision

**B**, unchanged from MVP-01, with the target now under the player's control.

**The camera has a target on the ground plane, in map units, that it is centred on.** Dragging
moves it; a tap on a digest entry or a selection moves it to the thing concerned; ADR-008's zoom
moves it to keep the anchor still. It is **clamped to the map bounds** the snapshot carries, with a
margin of one screen, so the galaxy cannot be scrolled out of sight. The MVP-01 `Follow()` is now
`LookAt()`; there is nothing to follow.

**The camera snaps to whole virtual pixels.** The target's projection is rounded with
`std::round` — not a cast, which truncates towards zero and puts a two-pixel jump at the origin —
and everything is drawn relative to the rounded value. Without it, at 2× a half-pixel error moves
alternate rows of a static object by a whole physical pixel and the whole screen crawls as the
camera pans.

**A map unit is not a metre.** The diorama is a tabletop: a system is a few units across, a lane a
few tens, a fleet mesh the size of a system so it can be tapped. Nothing is to scale and the ADR
says so, because the first reader who sees a 24-metre ship beside a star will ask.

**Depth is measured along the (1, 1, 1) viewing ray** and mapped affinely onto [0, 1] over
±4096 units centred on the target. **No depth bias is needed:** the meshes in the tree render with
clean silhouettes at 2× with `DepthBias` at zero because they have no coplanar surfaces, and every
mesh added for the map is built the same way.

## Consequences

**What this makes easy.** Picking (ADR-015) is exact: a tap un-projects to a ground point and a
target's screen bounds are the projection of its world bounds, both to the pixel.

**What this makes hard.** Dragging has to feel right at every zoom level with a camera that only
moves in whole pixels, which at 4 pixels a half-tile is coarse. Measured at MVP-02 step 6, not
argued here.

**What it costs.** A clamp, a drag gesture that `PointerInput` does not have, and a `LookAt()`
that the digest and the selection call.

**What it forecloses.** A perspective camera, and a rotating one. The diorama has one angle.

## What this changes elsewhere

- **Design/:** ADR-008 (zoom about a point), ADR-014 (the coordinates the generator emits),
  ADR-015 (the scene).
- **Code:** `NeuronClient/IsometricCamera.{h,cpp}` implements the projection, the snap and the
  depth range today, as `Follow()`. The clamp, the bounds and the drag are not implemented;
  `Design/Plans/MVP-02-TheLoop.md` step 6 adds them.

## Open questions

Whether the camera should ease to a `LookAt()` target over a few frames rather than jump. A jump
is honest and a glide is readable; decided by looking at it at step 6.
