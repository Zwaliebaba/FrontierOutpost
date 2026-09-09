# ADR-002 — A lit face is one of two palette entries, chosen per face

**Status:** Accepted

**Date:** 2026-09-09
**Decided by:** Build session MVP-01, step 4. Recommendation stated in `Design/Plans/MVP-01-IsometricShip.md` §3; this ADR takes it and records why, and records one thing the recommendation did not anticipate.
**Supersedes:** —

---

## Context

ADR-001 settles that every pass writes a palette index and never a color. That leaves the
question this ADR answers: a mesh has surfaces at different angles to a light, which is a
continuous quantity, and the screen has sixteen names. Something has to turn one into the other,
and where it happens decides what the ship looks like.

The palette makes one answer unusually cheap. The EGA default 16 is eight hues twice over: index
`n` for `n` in 0–7 is the dark variant and index `n + 8` is the bright variant of the same hue.
Dark blue `0000AA` is index 1 and bright blue `5555FF` is index 9; gray `AAAAAA` is 7 and white
`FFFFFF` is 15. The one place the pairing is not a pure brightness step is 6/14 — EGA's brown and
yellow — which is a property of the hardware palette rather than a mistake in the table. That
structure is asserted by `NeuronClientTests::PaletteTests::BrightHalfIsBrighterThanDarkHalf`,
because the whole scheme rests on it.

## Options considered

### A. Flat shading, one authored index per face, two tones

A face is authored with a palette index in 0–7. The light picks between that index and index + 8.
Nothing else is possible: there is no third tone, no gradient and no dithering.

The decision is made per face rather than per pixel, and the implementation makes that structural
rather than incidental — `MeshVS` computes it once per vertex from the face normal and marks the
result `nointerpolation`, so `MeshPS` has no arithmetic in it at all. There is no code path that
could produce a third value.

The cost is that a mesh carries at most eight distinguishable materials, and that a large curved
surface would band hard. Neither binds a twenty-four-triangle ship made of a hull, two wings and
two engine pods.

### B. Compute a lit color, then match it to the nearest palette entry

The conventional approach: shade normally into a `float3`, then search the sixteen entries for the
closest and write its index.

It is more general and it is what a bigger renderer would need. It also produces a *different*
picture, and mostly a worse one. Nearest-color matching over a 16-entry palette that is not
perceptually uniform hops between hues as the lit value slides: a gray hull passing through mid
brightness matches gray, then dark gray, then — because EGA has no mid-tones between `555555` and
`AAAAAA` — jumps. What reads as smooth shading in RGB reads as a face changing color when it is
quantized against this particular sixteen.

It also costs the property that makes A checkable. Under B, what index a face ends up as depends
on the light, the normal and the palette search together, and "this face should be white" is not
something a test can state. Under A it is: the face is index 7, and the light chooses 7 or 15.

Dithering between two indices is the usual remedy for B's banding and is rejected with it. It is
period-correct for a *static* image and wrong for a moving one: a dither pattern fixed in screen
space crawls across a surface as the ship moves, and one fixed in object space is a texture this
game has no way to author (R13).

## Decision

A face is authored with a palette index in **0–7**, the dark half of a pair. The light chooses
between that index and **index + 8**. Two tones, per face, and nothing between them.

`MeshVS` transforms the face normal by the world matrix, compares `dot(normal, lightDirection)`
against a threshold, and adds 8 when it passes. The result is `nointerpolation`, so every pixel of
a triangle gets the same index by construction rather than by the interpolator happening to agree.

The light is a single fixed direction in `MeshRenderer.h`. The threshold is 0.20 rather than 0.0,
so that the boundary does not sit at grazing incidence where a face turning slowly through it
would flicker between the two tones frame to frame.

**The light direction is part of this decision, not a detail under it.** The recommendation in the
plan did not anticipate this and it is worth writing down, because the first implementation looked
wrong for a reason that was not obvious. The camera looks along (1, 1, 1), so every face it can
see faces broadly upwards. A light from directly above therefore lights *all of them*, the hull
comes out a single flat tone, and a two-tone scheme produces a one-tone picture. The light has to
be raked across the hull — the value in `MeshRenderer.h` is (0.249, 0.549, −0.798), in from above
and from the port quarter — for the deck and the flanks to land on opposite sides of the
threshold.

**And the mesh has to have faces that differ.** A hull whose facets all tilt upwards has no two
tones to show under any light. `FrontierOutpost/ShipMesh.h` is built as a raised deck with flanks
falling away to the beam for exactly this reason; the first version was a stretched octahedron and
rendered as a uniformly white paper aeroplane. Measured on 2026-09-09 from the running executable:
the hull now covers 12,020 physical pixels of index 7 and 10,508 of index 15.

## Consequences

**What this makes easy.** The picture is checkable. A capture of the running game contains exactly
the indices the mesh was authored with and their +8 partners, and nothing else — verified on
2026-09-09: six colors on screen, indices 0, 4, 7, 9, 12 and 15, against a mesh authored with
hull 7, wings 1 and engines 4. A face that comes out the wrong color is a mesh or a light problem
with a short list of causes, not a shading model to debug.

**What this makes hard.** Eight materials per scene, and no smooth shading ever. A surface that
needs to read as curved has to be authored as facets. Anything wanting a third tone needs a new
ADR, because there is nowhere in this design to put one.

**What it costs.** The light is now a thing that has to be chosen *with* the mesh rather than
after it. That coupling is real: moving the light can flatten a hull that looked fine, and
reshaping a hull can flatten it under a light that was fine. Both directions were hit while
building this, which is why both are written down above.

**What it forecloses.** Per-pixel lighting of any kind, and therefore normal maps, specular
highlights and shadows. None of these can exist in a renderer whose output is a name rather than
a quantity (ADR-001).

## What this changes elsewhere

- **Code:** `NeuronClient/Shaders/MeshVS.hlsl` implements the choice; `MeshPS.hlsl` deliberately
  contains no arithmetic. `NeuronClient/MeshRenderer.h` holds the light and the threshold.
  `FrontierOutpost/ShipMesh.h` authors indices in 0–7 and says so.
- **AGENTS.md:** no change.
- **Design/:** no document superseded. ADR-001 is the decision this one sits on top of.

## Open questions

Whether a second light — a dim fill from the opposite side, choosing between the same two indices
on faces the key light misses — would read better than one hard light. Not tried; one light is
enough to make a twenty-four-triangle ship legible, and a second one is a change that can be made
without disturbing anything else here.

Nothing in this ADR decides what happens when two ships need different colors. The index is
authored into the mesh today, which means a second color scheme is a second mesh. That is fine for
one ship and will not be.
