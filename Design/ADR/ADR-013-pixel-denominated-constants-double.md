# ADR-013 — Every constant measured in pixels doubles, so the picture stays the size it was

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session for ADR-011. The owner asked for 1280×720; this ADR decides what
that does to the numbers that were expressed in virtual pixels, and is the decision the owner
should overturn first if the new framing is not what they wanted.
**Supersedes:** ADR-008

---

## Context

ADR-011 replaced a 640×400 framebuffer blown up 2× with a 1280×720 framebuffer presented 1:1. The
window is almost the same size on the desk — 1280×800 before, 1280×720 now — but **a pixel is not
the same thing on either side of that change.** Before today a "pixel" in this renderer was a
virtual one and reached the display as a 2×2 block; now it is one physical pixel.

Every constant in the tree that was denominated in virtual pixels therefore means half as much as
it did, and doing nothing about that is not a neutral choice — it is a decision to halve the
apparent size of everything on screen. Three families of number are affected:

**The camera's scale.** ADR-003 fixed it at 8 virtual pixels a ground unit and ADR-008 gave it a
ladder of five levels, `{4, 6, 8, 12, 16}`, every entry even so that the half-height is a whole
number and the un-projection stays exact. At 8 virtual pixels a unit the 10.75 m ship spanned 86
virtual pixels of a 640-pixel screen — 172 physical pixels, and 13% of the width.

**The starfield's densities.** ADR-010 chose masks `{2047, 4095, 8191}` — one pixel in 2048, 4096
and 8192 is a star — against a 640×400 target of 256,000 texels, which put 125, 62 and 31 stars of
each layer on screen. Each star was one virtual pixel and therefore a 2×2 block on the display.

**The size of text.** `FontRenderer` draws 8×8 glyphs one atlas texel to one virtual pixel, so a
glyph occupied 16×16 physical pixels and a line of the status display was 16 physical pixels tall.

## Options considered

### A. Double every pixel-denominated constant

The camera's ladder becomes `{8, 12, 16, 24, 32}` and the default becomes 16 pixels a ground unit.
The starfield's masks are multiplied by four — area, not length, so a mask goes to
`(n + 1) × 4 − 1`, giving `{8191, 16383, 32767}`. Text is drawn at `GLYPH_SCALE = 2`, so a glyph is
16×16 screen pixels.

Every figure comes out where it was. The ship spans 172 pixels of 1280 — 13% of the width, the
same fraction as before. The station's base drum is 102 pixels wide against 51 of a screen half
the size. `x − z = −26` still places the station a third of the way across the picture. The far
starfield layer holds 112 stars against 125, and a line of text is 16 pixels tall, as it was.

The ladder in particular is not really a change at all: `{8, 12, 16, 24, 32}` is what ADR-008's
levels always were *in physical pixels*. The units caught up with the numbers.

What it costs is that the extra resolution buys detail rather than field of view. A player sees
the same amount of space, rendered four times as finely.

### B. Leave the constants alone

The ship spans 86 pixels of 1280, the station's drum 51, a line of text 8. The screen shows four
times as much space.

This is what "we have more pixels now" would mean if the game were a strategy view whose problem
was that you could not see enough of the map. It is not. `Design/Archive/MVP-01-IsometricShip.md`
§2 has the camera follow one ship, the station is placed by projection arithmetic to sit a
specific distance from it (`StationMesh.h`), and ADR-002's whole argument about the light is about
a hull whose facets have to *read* — at 86 pixels across, with an 8-pixel status line under it.
Adopting B would silently invalidate the framing every one of those documents was written against,
and would do it as a side effect of a format change nobody asked to be a design change.

### C. Double some and not others

In particular: double the camera and the text, but leave the starfield masks, on the grounds that
stars are now one physical pixel rather than four and a denser field of finer stars might read
better than a sparser field of the same count.

This is a real aesthetic question and it is not obviously wrong. It is rejected here for the
narrow reason that it is a **look change smuggled into a format change**: 3.6× the stars is a
different backdrop, and if it is wanted it should be wanted on its own account, with somebody
looking at it, rather than inherited from arithmetic nobody did.

## Decision

**Every constant denominated in pixels doubles, so that the picture at 1280×720 is the picture at
640×400 blown up 2× — the same framing, four times the detail.**

Concretely:

- `IsometricCamera::ZOOM_LEVELS_PIXELS` becomes `{8, 12, 16, 24, 32}`, `DEFAULT_ZOOM_INDEX` stays
  2, and the default scale is **16 screen pixels a ground unit**. Every level is still even, so
  every property ADR-003 rests on — the 2:1 tile, the whole-pixel lattice, the exact
  un-projection — holds exactly as before, at five scales as ADR-008 established.
- `Starfield::LAYER_DENSITY_MASKS` becomes `{8191, 16383, 32767}`, each `(n + 1) × 4 − 1` of
  ADR-010's, so each is still one less than a power of two and the shader's test is still an `AND`
  rather than a modulo. `921600 / 8192` is 112 stars in the far layer against ADR-010's 125.
- `FontRenderer::GLYPH_SCALE` is **2**: one atlas texel is a 2×2 block of screen pixels. There is
  no sampler on the path — the pixel shader truncates an interpolated texel coordinate — so the
  enlargement is an exact block of pixels, which is the same integer divide the resolve pass did
  for the whole screen before ADR-011 deleted it.

**`GLYPH_SCALE` is the one of these that is a placeholder rather than a conclusion.** It is 2
because that is what text measured before today, and the owner is writing the UI design
separately. It is deliberately a single constant, with `CHARACTER_ADVANCE_PIXELS` and
`LINE_HEIGHT_PIXELS` derived from it, so that the UI design can settle it by changing one number.

**Nothing in world space moves.** `SHIP_SCALE`, the station's tier radii and `STATION_POSITION_X/Z`
are in metres and are untouched. The world did not change size; the camera looking at it did.

## Consequences

**What this makes easy.** Every measured figure in `Design/` and in the tree's comments stays
comparable across the change by doubling it, and the comments that quote pixel counts have been
updated to say so rather than being left to rot. `ShipMesh.h`'s "172 pixels of a 1280-pixel
screen", `StationMesh.h`'s "102 pixels of the 1280" and its placement derivation are all the old
numbers doubled, and say so.

**What this makes hard.** The extra pixels are not available as field of view. A session that
wants to see more space has to add a zoom level past 8 pixels a unit, and every level below 8 is
where ADR-003's guarantees start to matter most — a ship 86 pixels across at 8 px/unit is where it
was before today, and it was legible, so there is room. Note that the ladder is a *list*, so
extending it downwards is adding an entry, not changing a rule.

**What it costs.** Roughly four times the fill for the same picture, and four times the starfield
hash evaluations. Not measured, and not worth measuring: it is a fullscreen pass and a hundred and
sixteen triangles on hardware that runs D3D12.

**What it forecloses.** Nothing. Every number above is a constant in one place with the reasoning
next to it.

## Verification

Measured on 2026-09-10 from the running executable at 1280×720:

- The client area reports exactly 1280×720 to a DPI-aware observer, on a desktop at 125% scaling.
- A tap at client pixel (960, 520) — 320 right and 160 below centre — sent the ship to world
  (20.0, 0.0), which is what `(320 / 16, 160 / 8)` un-projects to at 16 pixels a ground unit. The
  status line read `X 20.0 Z 0.0` once the ship had settled. That is the whole input path — pointer
  message, client pixels, un-projection, order, server, replication — checked end to end at the new
  scale, with no present-scale division anywhere in it.
- The frame contains 8 colours, all of them names in `Color.h` (ADR-011 has the census).

## What this changes elsewhere

- **Code:** `NeuronClient/IsometricCamera.h` (the ladder), `NeuronClient/Starfield.h` (the masks,
  which also moved out of `StarfieldPS.hlsl` into root constants because a colour belongs with the
  other colours), `NeuronClient/FontRenderer.h` (`GLYPH_SCALE` and the two derived spacings),
  `FrontierOutpost/FrontierOutpost.cpp` (where the status text sits). `ShipMesh.h` and
  `StationMesh.h` have their quoted pixel figures doubled; no geometry changed.
- **AGENTS.md:** no change.
- **Design/:** ADR-008 superseded — its ladder is restated above in the units it always physically
  had. **ADR-003's "8 virtual pixels a ground unit" and ADR-010's `{2047, 4095, 8191}` are revised
  by this ADR and their status lines say so; the rest of both documents — ADR-003's derivation of
  the 2:1 projection and the snapping argument, ADR-010's case for a hash over a stored field — is
  untouched and still governs.**

## Open questions

Whether 16 pixels a ground unit is the right *default* now that the ladder reaches 32. Preserving
the old framing was the safe choice for a change that was supposed to be about format, but a
1280×720 game that opens one step further in is a perfectly reasonable thing to want, and it is
one constant.

Whether the starfield should be denser now that a star is one physical pixel rather than four —
option C above. Not tried, and it needs somebody to look at it rather than an argument.

What text should actually be, which is the UI design and is the owner's.
