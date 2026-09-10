# ADR-003 — The camera is 2:1 dimetric, written in whole pixels, and snaps to the pixel grid

**Status:** Accepted — the scale is revised by ADR-013 (8 virtual pixels a ground unit became 16 screen pixels when ADR-011 removed the 2× present scale). Everything else here — the 2:1 projection, its derivation, the whole-pixel lattice, the snap — stands unchanged.

**Date:** 2026-09-09
**Decided by:** Build session MVP-01, step 4. `Design/Plans/MVP-01-IsometricShip.md` §3 recommends "2:1 dimetric (yaw 45°, pitch `atan(0.5)` ≈ 26.565°)". This ADR keeps the 2:1 and departs from the angle, for the reason in *Options considered*.
**Supersedes:** —

---

## Context

The game presents a 640×400 screen scaled by a whole number (`Design/README.md` §1), and the
camera follows the ship: the ship stays centered and space scrolls under it (MVP-01 §2). Space is
unbounded, so there is no play area to clamp to and no horizon — every pixel on the screen is a
point on the `y = 0` plane, which is why a click cannot miss.

Two things depend on the exact projection rather than on its general shape. A click has to
un-project to a world point, and it has to be the *same* world point the ship was drawn at, or
the ship will not arrive where the player pointed. And the camera moves continuously while the
screen is a lattice of 640×400 virtual pixels, so something has to decide what happens between
two pixels.

## Options considered

### A. True isometric — the viewing ray along (1, 1, 1), elevation 35.264°

The projection with three equal axis foreshortenings. A one-unit square on the ground projects to
a diamond whose width-to-height ratio is √3 ≈ 1.732:1.

It is the mathematically tidy one and it is not what pixel-art games used, for a concrete reason:
1.732 is irrational, so the diamond's edges are not a repeating run of whole pixels. A tile edge
steps 1 pixel across for every 0.577 down, which no integer pattern reproduces; tiled ground and
long straight edges alias into a visibly irregular staircase at 2× where every virtual pixel is a
2×2 block.

### B. 2:1 dimetric — the ground diamond exactly twice as wide as it is tall

The projection those games actually used, and near-universally miscalled isometric. The diamond is
16 pixels wide and 8 tall at this zoom, so a tile edge is an exact 2-across-1-down staircase.

Obtained from A by squashing the vertical screen axis by √3/2 ≈ 0.866. Equivalently it is a pure
rotation of yaw 45° and elevation **arcsin(0.5) = 30°** — and this is where the plan's
parenthetical does not hold up. For a yaw of 45° and an elevation θ, the ground diamond's
width-to-height ratio is 1/sin θ. The plan recommends θ = `atan(0.5)` ≈ 26.565°, and
1/sin(26.565°) = **2.236**, not 2. `atan(0.5)` is the angle whose *tangent* is one half, which is
a different quantity from the one that makes a tile 2:1. Getting 2:1 needs sin θ = 0.5, so
θ = 30°.

The two halves of the recommendation are therefore inconsistent, and the 2:1 is the half worth
keeping: it is the thing the recommendation is *named* after, it is what makes the pixel
arithmetic exact, and 2.236:1 has the same irrational-staircase problem as A with none of A's
tidiness.

### C. Perspective

Not seriously considered, and recorded so nobody proposes it as new. A perspective camera makes
the un-projection depend on depth, makes a ship's on-screen size depend on where it is, and makes
the scale fractional everywhere. Every one of those is something `Design/README.md` §1 rules out.

## Decision

The camera is **orthographic and 2:1 dimetric**, and it is written as an exact relationship in
whole virtual pixels rather than derived from a yaw and a pitch:

```
pixelX = (x - z) * HALF_TILE_WIDTH_PIXELS      // 8
pixelY = (x + z) * HALF_TILE_HEIGHT_PIXELS     // 4
       - y       * HEIGHT_PIXELS_PER_UNIT      // 8
```

Stating it this way rather than as trigonometry is the decision, not a shortcut. Every coefficient
is a whole number of pixels, so the projection of a lattice of world points is a lattice of pixels,
and the inverse is two divisions with no accumulated error. The unit tests in
`NeuronClientTests::IsometricCameraTests` compare with exact float equality and pass, which is a
claim that could not be made about a matrix built from sines.

The equivalences, so the number can be recognised: the null direction of that map — the direction a
viewing ray runs — is **(1, 1, 1)**, so this is true isometric with the vertical screen axis
squashed by √3/2. As a rotation it is yaw 45°, elevation 30°. The **world-units-per-pixel** it
implies is 1/8 of a unit horizontally along a ground axis and 1/8 of a unit of height per pixel
vertically; one world unit is one metre, so the 24-metre ship spans about 160 of the 640 pixels.

**The camera snaps to whole virtual pixels.** `Follow()` rounds the target's projected position
with `std::round` and everything is drawn relative to the rounded value. It has to: at 2× a
half-pixel camera error moves alternate rows of a static object by a whole physical pixel, and the
whole screen crawls as the ship drifts. `std::round` rather than a cast, because a cast truncates
towards zero and would put a two-pixel jump at the origin where the sign changes.

Depth is measured along the (1, 1, 1) viewing ray and mapped affinely onto [0, 1] over
±4096 units **centered on the target**, not on the world origin — space is unbounded and the ship
never comes back, so a depth range pinned to the origin would run out.

**No depth bias is needed**, which `Design/Plans/MVP-01-IsometricShip.md` §6 left open to be found
out at this step. It was: the hull renders with clean silhouettes at 2× with `DepthBias` at the
D3D12 default of zero. The reason there is nothing to fix is that the mesh has no coplanar
surfaces — the only near-coincident geometry is the wing roots, and those are buried inside the
hull rather than resting on it.

## Consequences

**What this makes easy.** Un-projection is exact and testable without a device: the center of the
screen is the ship, a known pixel is a known offset, and the same pixel after the ship has moved is
a different world point by exactly the ship's displacement. All three are tests, and all three are
the properties step 6's click depends on.

**What this makes hard.** Zoom is not free. Changing `HALF_TILE_WIDTH_PIXELS` to anything that is
not an even number breaks the 2:1 into half-pixel steps, so the zoom levels this camera can have
are 2, 4, 6, 8 … and not a continuous range. That is a real restriction and it is the same one the
integer present scale imposes.

**What it costs.** The projection is not a pure rotation, so a normal transformed by it is not a
normal in view space. Nothing does that today — lighting happens in world space (ADR-002) — but
anything that wanted view-space shading would have to be told.

**What it forecloses.** Rotating the camera. A 2:1 dimetric camera at a yaw other than 45° has no
whole-pixel form, so "let the player turn the view" is a new ADR and a different projection.

## What this changes elsewhere

- **Code:** `NeuronClient/IsometricCamera.{h,cpp}` is this ADR. `NeuronClient/Shaders/MeshVS.hlsl`
  consumes the matrix. `FrontierOutpost.cpp` calls `Follow()` with the ship's position each frame.
- **AGENTS.md:** no change.
- **Design/:** this ADR closes the depth-bias question in `Design/Plans/MVP-01-IsometricShip.md`
  §6, and departs from the pitch in that plan's §3 recommendation as argued above.

## Open questions

Whether the camera should lag the ship rather than being locked to it. Locked is right for the
MVP — it is the simplest thing that makes the click arithmetic testable — but a camera that eases
towards the ship reads better at speed, and it interacts with the snapping in a way that has not
been thought about: an eased camera is at a fractional position almost always, so the snap would
be doing real work every frame rather than almost none.
