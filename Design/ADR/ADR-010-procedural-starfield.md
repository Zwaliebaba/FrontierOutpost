# ADR-010 — The starfield is a hash, in three parallax layers

**Status:** Accepted — the layer densities are revised by ADR-013 (`{2047, 4095, 8191}` became `{8191, 16383, 32767}` when the screen grew from 640×400 to 1280×720), and the layer colors are colors rather than palette indices and now live in `Starfield.h` (ADR-011). The decision itself — a hash rather than a stored field, three parallax layers, why it does not shimmer — stands unchanged.

**Date:** 2026-09-10
**Decided by:** Build session, at the owner's request for a starfield.
**Supersedes:** —

---

## Context

ADR-003 mentioned the starfield before one existed. Deciding that the camera snaps to whole
virtual pixels, it gave the reason as:

> Decide whether the camera snaps to whole virtual pixels as it follows — it should, or the
> starfield swims

So the hard part was settled a week before the feature: whatever the backdrop is, it has to be a
function of a whole number of pixels, or every star resamples itself every frame and the screen
crawls.

Two other constraints bind. Space is unbounded (MVP-01 §2) — the ship can fly in one direction
forever and never come back — so a backdrop cannot be a finite thing the ship might reach the edge
of. And R13 says the executable ships alone: there is no file to put a star texture in.

There was also a gap the starfield closes. Until now the camera followed the ship across an
otherwise empty black screen, so a ship at full speed looked identical to a ship standing still;
the station helped, but only while it was on screen.

## Options considered

### A. A stored list of stars

Generate some thousands of star positions at startup, keep them in a buffer, draw them as points.

Straightforward, and it is what most games do. It does not survive unbounded space: either the
list covers a finite region the ship flies out of, or it wraps — and a wrapping list is a repeating
pattern, which is far more obvious against a black sky than it sounds. Making it large enough to
hide the repeat is a lot of memory to solve a problem the next option does not have.

### B. A screen-space starfield that does not move

Stars fixed to the screen. Costs nothing and is trivially stable.

It is also the one thing the backdrop must not be. A backdrop that does not move relative to the
ship gives no sense of motion at all, which is precisely the gap this feature exists to close.

### C. A hash of the texel's position, in parallax layers

For each texel of the 640×400 index target, hash its position in a layer's own scrolled
coordinates and light it if the hash comes up short. Several layers, each scrolling at a fraction
of the camera's rate.

Nothing is stored, nothing repeats within any distance the ship can reach, and it costs one
fullscreen pass that replaces a clear the frame was doing anyway.

## Decision

**C.** `NeuronClient/Starfield` draws one fullscreen pass into the index target before anything
with depth. `StarfieldPS.hlsl` hashes each texel's position and lights it if `hash & mask` is zero.

**Three layers**, scrolling at 1/8, 1/4 and 1/2 of the camera's rate — divisors `{8, 4, 2}`.
Nothing scrolls at the camera's full rate, because that would put a star in the same plane as the
ship. Density falls and brightness rises towards the viewer: masks `{2047, 4095, 8191}` and palette
indices `{8, 7, 15}` — dark gray, light gray, white. All three are grays on purpose; a colored star
competes with the ship, and the ship is the thing the eye has to find.

**The scroll offsets are computed on the CPU, in integers, with a floor division.** This is the
part with a bug in it if anywhere does. C++ division truncates towards zero, so with a divisor of 8
a truncating implementation returns 0 for every camera position from −7 to +7 — a fifteen-pixel
band where the layer holds still and then jumps two steps as the ship crosses the origin. It is
the same argument that made `Follow()` use `std::round` rather than a cast, one level further out,
and `StarfieldTests` pins it across a range spanning zero for every layer.

**The stars are behind the text and the meshes**, drawn first, and the pass neither tests nor
writes depth — it leaves the depth buffer exactly as `BeginScene` cleared it for the mesh pass.

## Consequences

**What this makes easy.** Motion is legible. The ship now moves against something at every position
in unbounded space, not only while the station is on screen.

**What it costs.** One fullscreen pass over 256,000 texels, doing three hashes each. It replaces
the clear that pass would otherwise have done, so the marginal cost is two extra hashes a texel;
not measured, because at this resolution there is no measurement that would mean anything.

**What this makes hard.** The star field cannot be authored. There is no way to place a
constellation, a nebula or a landmark — every star is wherever the hash puts it. A game that wants
a named star has to draw it as something else.

**What it forecloses.** Nothing that was wanted. Note that stars do not scale with zoom: at any
zoom a star is one virtual pixel, and only the scroll rate changes. That is right for a backdrop
and would be wrong for anything meant to be *in* the world.

## Verification

Two properties, both measured on 2026-09-10 rather than asserted.

**It does not shimmer.** Two separate runs of the executable with the ship at the same position
produced frames that differ by **zero pixels** across the whole screen below the status line. The
backdrop is a pure function of the snapped camera position, and identical across processes — which
is what ADR-003's snapping bought.

**It parallaxes.** After flying the ship to (22.2, −9.7), not one star in a sampled quadrant was
still where it had been, in any of the three layers.

Every frame still passes the two standing checks: every pixel one of the EGA 16, and every 2×2
block of physical pixels uniform.

## What this changes elsewhere

- **Code:** `NeuronClient/Starfield.{h,cpp}`, `NeuronClient/Shaders/Starfield{VS,PS}.hlsl`.
  `IsometricCamera` gains `SnappedTargetXPixels()`/`Y()`, which exist for this and say so.
  `FrontierOutpost.cpp` draws it first, inside `BeginScene`/`Resolve`.
- **Design/:** nothing superseded. ADR-003 anticipated this and its snapping decision is what makes
  it work; this is the first thing in the tree that would have failed without it.
- **AGENTS.md:** no change.

## Open questions

Whether the density and the three brightnesses are right. They were chosen by looking, and looking
is the only way to choose them; they are four numbers in `StarfieldPS.hlsl`.

Whether the starfield belongs in `NeuronClient` at all. A procedural backdrop is generic renderer
work, like `FontRenderer` — but "space looks like this" is a game decision, and the palette indices
and densities that encode it currently live in the engine. If a second look is ever needed, those
move out to the game and the pass takes them as parameters. Not done now because one caller does
not justify the interface (R2).
