# ADR-106 — A glint, a dart, and a dither that did not earn its place

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision, 2026-09-14: close the open items ADR-105 left and take up the one it called most interesting. Two of the three were built; the third was built, measured and thrown away, which is the part of this document worth reading.

**Supersedes:** — It **amends ADR-105** (a fifth tone) and **closes three of its five open questions**. It also settles, on measurement rather than on argument, the dithering question ADR-002 opened in 2026-09-09 and ADR-104 reopened.

---

## Context

ADR-105 left five questions open and called one of them — object-space dithering — the most
interesting, because it is the only technique that buys tonal gradation without needing band width,
and band width is the binding constraint on a ball 8 to 12 canvas pixels across.

It also observed that a stepped specular was the one addition that does not compete for width,
because a highlight is a spot rather than a band, and that `Octahedron` had been written, tested and
drawing nothing since ADR-103 while fleets were still flat arrowheads in the overlay layer, drawing
over the very stations they were behind.

One thing had to be settled before any of it: **whether four bands survive magnification.** ADR-075
presents the canvas at a whole-number scale by an integer texel load and an integer divide, which
means a nearest-neighbour upscale of a scale-1 capture is not an approximation of the scale-3
presentation — it *is* it, pixel for pixel. So the question was answerable without a 4K panel, and
the answer is that the four bands read as clean, distinct stripes at 3×. The banding is stark but it
is legible and deliberate, which is the cel look and not a defect.

## Options considered

### The dither, and why it is not here

The idea was strong and the argument for it in ADR-104 still is: it interleaves two authored tones
rather than mixing them, so **every pixel remains a colour somebody named** — a stronger guarantee
than a fifth tone gives, and one a capture can still be counted against. ADR-002 had rejected it
because a screen-space pattern crawls and an object-space one "is a texture this game has no way to
author"; the interpolated normal and world position ADR-103 added are an object-space coordinate, so
the pattern is computed and R13 never comes into it. That objection was genuinely answered.

It was built: a triplanar 4×4 Bayer indexed by the world position on the two axes least aligned with
the normal, with each of the existing thresholds becoming a dithered zone rather than a hard step.
Three variants were captured and measured on the same board and tick.

| variant | isolated single pixels on the ball |
|---|---|
| no dither (ADR-105 as committed) | 0.4% |
| 4×4 Bayer, zone width 0.12 | 2.8% |
| 4×4 Bayer, zone width 0.30 | 13.5% |
| 2×2 Bayer, zone width 0.25 | 9.9% |

**A narrow zone was invisible and a wide one was speckle, and the reason is arithmetic rather than
taste.** On a ball of nine pixels' radius the dot product changes by about 0.11 per pixel, so a zone
of ±0.12 is two pixels wide — there is no room in it for a pattern. Widening it to ±0.30 gives about
five pixels, and a 4×4 ordered matrix needs four pixels per period, so the whole transition holds
roughly one period. One period of an ordered pattern is not a weave; it is a handful of scattered
pixels. A 2×2 matrix halves the period and improves it, and it is still scattered, because the
sphere's projection compresses the world-space lattice unevenly toward the limb and the triplanar
axis pair switches partway across the ball.

The isolated-pixel figure is reported for what it is and not more: a true checkerboard would also
score high on it, so it measures how much pixel-level alternation there is rather than how ordered
it is. The judgement that it reads as fuzz rather than as weave is from the magnified captures, and
those are the evidence.

**So the rejection is not "dithering is wrong here".** It is that *this ball is too small for it*,
and the condition for revisiting is a number: an ordered dither wants a transition several periods
wide, which on a 4×4 matrix means a ball of roughly 25 pixels' radius — about three times what the
map's default framing gives, and reachable only near the top of the zoom range (`ZOOM_NEAREST` is
2.5). A dither that switched on with zoom would be a surface that changes material as you lean in,
which is worse than not having one.

### The glint

A thresholded Blinn-Phong lobe: `dot(normal, normalize(light + toEye))` over 0.97, which is a cap
fourteen degrees across and a blob four or five pixels wide on a nine-pixel ball. Taken.

**It is a spot, not a band, and that is the whole of why it fits where a fifth diffuse step would
not.** ADR-105 said the next tone would have to prove itself, and this is the proof: measured on the
committed board, the glint covers 10 of Torvald's 255 pixels and takes them from the lit band alone,
so nothing else on the ball gets narrower. It is the first thing in this sequence that makes a
station read as a *hard* surface rather than a matte one.

It is switched off for flat-faced solids by passing the lit tone, exactly as the rim is switched off
by passing the shadow: a flat face's half-vector dot is constant across its whole area, so a face
that crossed the threshold would turn white all at once rather than carrying a highlight.

### The fleets

**A. Leave them as flat arrowheads.** They are in the overlay layer, so a fleet behind a station
draws in front of it — the one common occlusion artifact left after ADR-103.

**B. Use `Octahedron` as it was.** Rejected, and the reason is worth recording because the primitive
was sitting there inviting it: a symmetric solid **throws away the one thing the flat triangle was
carrying**, which is which way the fleet is going. Fixing an occlusion bug by deleting information
is not a fix.

**C. Make the octahedron directed.** Taken. It gains a `_forward` in the ground plane and separate
length and width, so it is a dart along the lane; equal length and width give the old symmetric
solid back. The fleet is then depth-tested with the stations, catches the same light, and still
points where it is going — in world space, so it keeps meaning "that way" as the camera orbits,
which is what the projected arrowhead was careful about too.

## Decision

**A lit surface is one of FIVE authored colours**: the shadow, the band the light grazes, the band it
finds, the silhouette it gets past, and the glint it bounces straight back. The shader selects; it
never mixes, scales or interpolates. `Ink::Glinted` is the owner's colour 0.75 of the way to white,
the brightest thing on a station, and `Ink::FlatRampFor` switches both the glint and the rim off for
a flat-faced solid.

**A fleet under way is a directed solid** — `MeshRenderer::Octahedron` stretched along its lane, 10
world units long and 4.4 across — drawn in the mesh pass with the stations. Its tether, its label and
its hit rectangle are unchanged; the flat arrowhead is gone.

**There is no dither.** The measurement above is the reason, and the condition for revisiting it is
recorded rather than the idea being closed.

## Consequences

**What this makes easy.** Every solid in this world now reads as a solid: the stations are hard
spheres on lit columns, and the fleets are darts that occlude and are occluded correctly. Anything
else that comes to stand here gets all five tones and the depth test for free.

**What this makes hard.** Nothing new, but the ceiling is now visible and worth stating plainly: the
diffuse ramp is full at this ball size. The bands are two pixels; a sixth tone would be one, and the
dither that could have escaped that constraint has been measured and cannot. **The next real
increase in fidelity is a bigger ball, not another tone** — which is a statement about the map's
framing rather than about the shader.

**What it costs.** Four more bytes a vertex (40 → 44) and a seventh input element, one dot product
and one compare per lit pixel, and thirty vertices a fleet in the mesh pass instead of three in the
shape pass.

**What it forecloses.** Nothing that was wanted, and the dither explicitly stays open on a stated
condition.

## Verification

Measured on 2026-09-14, Debug|x64, from the running client at `--scale 1` joined to a
`--serve --phase0 --bots 5` peer as seat `bravo`:

- **Magnification**: a nearest-neighbour 3× of the scale-1 canvas — which is exactly what the resolve
  pass produces (ADR-075) — shows the four bands as distinct, legible stripes. The open question
  ADR-105 recorded is answered: they survive, as cel banding rather than as smoothness.
- **The glint**: 10 of Torvald's 255 ball pixels, at `215,240,255`, which is `Ink::Glinted` of the
  owner blue to the byte. Taken from the lit band only; the other three bands are unchanged from
  ADR-105's measurement.
- **The dither**: three variants captured and counted, table above, and rejected.
- **The darts**: photographed on a board with fleets in transit, pointing along their lanes and
  shaded by the same light. The occlusion fix is structural rather than photographed — a fleet is
  now in the depth-tested pass — and no capture in this session happened to place a fleet directly
  behind a station.
- All five suites pass, 615 tests, including a new one that a directed octahedron reaches its length
  along the heading it was given and its width across it, at forty-five degrees so an implementation
  that quietly used an axis would fail.
- `CheckFormat`, `CheckProjectFiles` and a whole-tree `RunClangTidy` (80 translation units) are clean.

## What this changes elsewhere

- **Code:** `NeuronClient/Color.h`'s `ColorRamp` gains `glint`. `MeshRenderer`'s vertex gains
  `glintColor`; `Octahedron` gains a direction and separate length and width. `MeshBackend` gains an
  input element. `MeshVS`/`MeshPS` carry and select five tones. `LockstepClient/DesignTokens.h` gains
  `STATION_GLINT` and `Ink::Glinted`. `MapRender.cpp` draws a fleet as a dart in the mesh pass and
  its overlay no longer draws an arrowhead.
- **Design/:** `UI/DESIGN-GUIDELINES.md`'s Map section describes five tones and the dart. ADR-105's
  status line records what is amended and which of its open questions are closed.
- **AGENTS.md:** no change.
- **Captures:** the no-finger captures that show the map are retaken.

## Open questions

**The dither, on a stated condition:** a ball of roughly 25 pixels' radius, which the map's framing
does not give. Revisit it if the framing changes, not before — and not by switching it on with zoom.

**The fill light**, unchanged from ADR-105 and still carrying its number: anything proposing one has
to beat 7% of the ball at the opening framing without swallowing the shadow a quarter turn round.

**Whether the diffuse ramp should shrink again.** Four bands at two pixels each is a lot of
machinery for a small ball, and it is possible that three read better and the fourth is only
visible at 3×. Nobody has looked at that question the other way round.

**The legend still draws a flat triangle for `FLEET UNDER WAY`**, where the map now draws a dart.
`Design/UI/DESIGN-GUIDELINES.md` says the legend draws the shape it is naming (ADR-090), and the
legend is shape-pass geometry that cannot hold a solid. A triangle is a fair symbol for a pointed
thing that moves and it is left as one deliberately, but it is drift and it is recorded as drift.

**What else stands in the world.** Site pins in the sealed region and the capital's halo are still
flat; the rings and the garrison badges are still overlay ink that draws over a nearer ball.
