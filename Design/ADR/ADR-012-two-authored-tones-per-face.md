# ADR-012 — A lit face is one of two authored colours, chosen per face

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session for ADR-011. The owner decided that the named colours survive the
move to R8G8B8A8; this ADR takes ADR-002's two-tone scheme across to a world with no palette and
records what had to change to keep it.
**Supersedes:** ADR-002

---

## Context

ADR-002 decided that a face is authored with a palette index in 0–7 and the light chooses between
that index and index + 8. It worked because of a property of one specific table: the EGA default
16 is eight hues twice over, so `n` and `n + 8` are the dark and bright variant of the same hue,
and "brighten this face" was **an addition**. `MeshVS` did the addition once per vertex, marked the
result `nointerpolation`, and `MeshPS` had no arithmetic in it at all — so there was no code path
anywhere that could produce a third tone.

ADR-011 removed the palette. There is no index to add eight to. The question this ADR answers is
what "the light chooses between two tones" means when a tone is four bytes rather than a small
integer — and, because the arithmetic that used to imply the second tone is gone, where the second
tone comes from at all.

The colours themselves did not change. `Color.h` holds the same sixteen values under the same
sixteen names (ADR-011), so a hull that was `AAAAAA` lighting to `FFFFFF` is still exactly that.

## Options considered

### A. The vertex carries both tones; the shader picks one

A face is authored as a `ColorPair` — a shaded colour and a lit colour, both written out. Both are
packed onto every vertex of the face, as two `R8G8B8A8_UNORM` input elements. `MeshVS` compares
`dot(normal, lightDirection)` against the threshold and selects one; the result is
`nointerpolation` and `MeshPS` returns it unchanged.

This keeps ADR-002's guarantee **structurally** rather than by convention: the only two colours the
pass can emit are the two that arrived on the vertex, and there is no arithmetic between them for
an edit to widen into a gradient. It is the same shape as before with the addition replaced by a
selection.

It costs four bytes a vertex — the vertex goes from 28 to 32 bytes — and it costs saying the pair
out loud at every authoring site. On a 24-triangle ship and a 92-triangle station, neither is a
real number; the second is arguably a gain, since `{LIGHT_GRAY, WHITE}` says what it means where
`7` did not.

### B. The vertex carries one colour; the shader brightens it

Author one base colour and have the shader multiply by, say, 1.0 when lit and 0.66 when shaded.
One colour a vertex, and any material at all works without authoring a second tone.

This is the conventional answer and it is what a bigger renderer would do. It is rejected because
it puts arithmetic back on the colour, and that arithmetic is exactly what ADR-002 spent its length
keeping out. A multiply that produces two values today produces three the moment somebody
interpolates the factor, and nothing in the code would object. It also produces *worse pairs*: the
EGA relationship between `AAAAAA` and `FFFFFF` is not a multiply, and `0000AA` to `5555FF` is not
one either — both add to the dim channels rather than scaling the bright one. Reconstructing the
game's existing look through a scale factor is not possible, and changing the look was not part of
the request.

### C. A table of pairs, and the vertex carries an index into it

Keep an index on the vertex, but index a table of `ColorPair` rather than a palette of colours.

This is ADR-002 with one level of indirection added, and it reintroduces the limit ADR-011 removed
— a material is now a slot in a fixed table rather than a colour. It also needs the table bound to
the mesh pass as constants, which is machinery in service of saving four bytes on a vertex. It
would earn its place if there were hundreds of materials and the pair had to be swapped at runtime,
which is the day a ship gets a faction colour scheme. That day is not today.

## Decision

A face is authored as a **`Neuron::ColorPair`: a shaded colour and a lit colour, both written
out**. Both are packed onto each of the face's three vertices as `R8G8B8A8_UNORM` attributes.
`MeshVS` transforms the face normal by the world matrix, compares `dot(normal, lightDirection)`
against the threshold, and selects the lit colour when it passes. The result is `nointerpolation`,
so every pixel of a triangle gets the same colour by construction rather than by the interpolator
happening to agree, and `MeshPS` returns it with no arithmetic — unchanged in spirit from ADR-002.

Two tones, per face, and nothing between them.

The light and the threshold are unchanged and still live in `MeshRenderer.h`: direction
(0.249, 0.549, −0.798), threshold 0.20. **ADR-002's paragraph on why the light is raked across the
hull rather than pointed down at it, and why the mesh has to have faces that differ, is still true
and still load-bearing** — it is a property of the camera and the geometry, not of the palette, and
a session that flattens the light will flatten the ship exactly as it would have before.

Which of a pair is which is no longer guaranteed by the palette's structure, so it is asserted
instead: `ShipMesh.h` and `StationMesh.h` carry `static_assert`s that every pair's lit tone has a
higher `Luminance()` than its shaded tone, and `NeuronClientTests::ColorTests` makes the same
claim about the eight hue pairs in `Color.h`. A pair written the wrong way round is a build error
rather than a ship that is somehow darker where the light hits it — which is exactly the kind of
wrongness that reads as a deliberate art choice.

## Consequences

**What this makes easy.** A material is now any two colours rather than one of eight hues. The
eight-materials-per-scene limit in ADR-002's consequences is gone with the palette, and a pair
that is not a hue and its bright variant — a dark blue lighting to a pale gray, say — is now
expressible. Nothing in the tree uses that yet; the ship and the station are the same three pairs
they always were.

Authoring also got more legible. `{LIGHT_GRAY, WHITE}` needs no comment; `7 // AAAAAA, lighting to
FFFFFF` needed one, and that comment was the only thing linking the number to the colour.

**What this makes hard.** Still no smooth shading, ever, and still no third tone. A surface that
needs to read as curved is authored as facets. That is unchanged and deliberate.

The new soft spot is that the pair is now two independent values, so it can be *inconsistent* in a
way an index pair could not: two faces of the same material can disagree about what "lit" means if
somebody types one of the four values wrong. The `static_assert`s catch the pair being inverted;
they do not catch a hull whose two halves are a slightly different gray. Naming the pairs once per
mesh, as `HULL_COLOR` and friends do, is what keeps that from being reachable.

**What it costs.** Four bytes a vertex, and every authoring site now states both halves.

**What it forecloses.** Per-pixel lighting of any kind, and therefore normal maps, specular and
shadows — unchanged from ADR-002. The reason has changed, though, and it is worth being honest
about: under ADR-002 those were impossible because the output was a name. Now they are merely
*decided against*. A session that wants one is writing an ADR, not hitting a wall.

## What this changes elsewhere

- **Code:** `NeuronClient/Color.h` defines `ColorPair`. `NeuronClient/Mesh.h`'s `MeshVertex`
  carries `shadedColor` and `litColor`. `NeuronClient/Shaders/MeshVS.hlsl` selects;
  `MeshPS.hlsl` still contains no arithmetic. `NeuronClient/MeshRenderer.cpp` declares the two
  `R8G8B8A8_UNORM` input elements. `FrontierOutpost/ShipMesh.h` and `StationMesh.h` author pairs.
- **AGENTS.md:** no change.
- **Design/:** ADR-002 superseded. ADR-011 is the decision this one sits on top of.

## Open questions

ADR-002's two open questions are both still open and both still worth their space.

Whether a second light — a dim fill from the opposite side, choosing between the same two tones on
faces the key light misses — would read better than one hard light. Not tried.

What happens when two ships need different colours. The pair is authored into the mesh, so a
second colour scheme is a second mesh. That was fine for one ship and will not be; option C above
is the shape of the answer, and it is written down so the session that needs it does not start
from nothing.
