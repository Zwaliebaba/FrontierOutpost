# ADR-108 — A fourth diffuse band, and the shadow stops being flat

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner, on the 3D map: *"the spheres in the 3D scene only have 2 or 3 shades. To make it look more natural, I wonder if we can add more shades of the shadow."*
**Supersedes:** — (amends ADR-105's ramp; answers one of ADR-106's open questions)

---

## Context

A station is a lit solid drawn by a mesh pass that picks one authored tone per pixel and never mixes
two (ADR-011, ADR-012). The ramp has grown twice: ADR-104 added the silhouette, ADR-105 added the
band the light grazes, and ADR-106 added the glint — and ADR-105 said in terms that the next tone
would have to prove itself, which the glint did only by being a SPOT rather than a band. A spot does
not compete for band width; a diffuse step does.

**The owner's reading was that a ball shows two or three shades, and measuring the pixels says that
is right about the part of it that matters.** Counted off `01-build-sheet.png` on 2026-09-14, a
near ball carried lit 39%, half-lit 21%, shadow 20%, rim 16%, glint 4%. Five tones — but only three
of them are the diffuse ramp, and the **dark side was one flat tone** with a 72-unit jump from it to
the rim, which reads as an edge rather than as a shade. So the ball went bright, mid, flat dark,
bright edge, and the flat dark was a fifth of it.

Two figures bound what follows, both measured rather than inherited. **The balls are bigger than the
record says.** ADR-105 and ADR-106 argued against a fifth band on "an 8-12 pixel ball" and
"Torvald's 255 pixels", which is a radius near 9. At the authored framing today a capital is
**r≈16.5** and an ordinary system **r≈10.6 to 14.9**, so a band has roughly twice the pixels those
decisions assumed. And ADR-106's dither was rejected on a stated condition — a ball "of roughly 25
pixels' radius, which the map's framing does not give" — which is nearer than that sentence implies.

## Options considered

### A. Split the shadow in two

One more authored tone between `shaded` and `halfLit`, one more threshold on the light, tested after
the rim so the silhouette keeps the extent ADR-104 tuned for it. The dark side becomes two bands
instead of one.

It costs a sixth tone on every mesh vertex — `ColorRamp`, `MeshVertex`, the input layout, both
shaders — and it makes every capture with a station in it stale. And it is exactly the thing ADR-105
put a bar in front of: a diffuse band competes for width with the two above it.

### B. Make the rim carry the dark side instead

Retune `RIM_TERMINATOR` so the silhouette reaches further round and the flat shadow is smaller. No
new tone, no vertex change, no wire of any kind — one constant.

It is the wrong shape for the problem. The rim is measured from the EYE and the bands from the
light, which is the whole distinction ADR-104 drew; widening it does not add a step to the ramp, it
moves a boundary that turns with the camera. A ball would get brighter at the limb as the shadow
shrank, which is the opposite of "more shades of the shadow".

### C. The dither ADR-106 built and threw away

An ordered 4×4 matrix across the terminator, which is a gradient made of two authored colours rather
than a third colour nobody chose. It was measured and rejected: isolated single pixels went from
0.4% to 13.5%, because the dot product moves 0.11 a pixel on a small ball and the whole transition
held about one period of the matrix.

Its stated condition is r≈25 and the framing gives 10.6 to 16.5, so it is still not met. It is also
strictly harder than A for the same end, and A's own measurement below is what a dither would have
had to beat.

### D. Do nothing, or go the other way

ADR-106 left a live question pointing the other way — *"whether the diffuse ramp should shrink
again. Four bands at two pixels each is a lot of machinery for a small ball, and it is possible that
three read better."* Nobody had looked, and the honest thing was to look while adding rather than
after.

## Decision

**A.** `ColorRamp` grows a sixth tone, `quarterLit`, between the shadow and the grazed band, and
`MeshPS.hlsl` grows a third threshold on the light — `DUSK_TERMINATOR`, tested after the rim so the
silhouette is untouched. The diffuse ramp is four bands; the ball is six tones.

**The three diffuse steps are now equal.** `STATION_HALF_SHADE` is 0.30 and `STATION_SHADE` is 0.58,
so the new `STATION_QUARTER_SHADE` is **0.44** — 0.14 at a time down the same ramp toward the
background. Even steps rather than a number chosen for this band alone, because the eye reads a ramp
by its evenness and putting the new tone anywhere else would have made one of the three boundaries
louder than the other two. In blue that is 255 → 185 → 150 → 119 with 31 and 35 between neighbours.

**The threshold is −0.10, and it was measured rather than argued.** The first value tried was −0.30
and it ate the band it was splitting: the new tone took 15% of the ball and left the shadow a 4%
sliver, so the ramp still ended in a step rather than a gradation. −0.10 halves the old 20% instead.

**What it measures, at four ball sizes, on 2026-09-14:**

| ball | radius | lit | grazed | **new** | shadow | rim | glint |
|---|---|---|---|---|---|---|---|
| Dothan, a capital | 16.5 | 31% | 25% | **9%** | 11% | 20% | 5% |
| Pell | 14.9 | 25% | 26% | **9%** | 14% | 21% | 5% |
| Faroe | 11.9 | 34% | 27% | **7%** | 7% | 19% | 5% |
| Xander | 10.6 | 24% | 25% | **8%** | 18% | 20% | 4% |

The new band is **2.2 pixels wide at r=16.5 and 1.3 at r=10.6**, which is thinner than ADR-105's
bands and thin enough to have been the objection. **It is kept on the isolated-pixel count**, which
is the measurement ADR-106 rejected the dither on: **0.0% of the new band's pixels are isolated at
every one of the four sizes** — identical to the grazed band that is already accepted. A band 1.3
pixels wide that is contiguous is a band; the dither's 13.5% was speckle. That is the whole of why
this clears the bar ADR-105 set and the dither did not.

## Consequences

**A mesh vertex carries six tones and the input layout has eight elements.** That is 24 bytes of
colour per vertex, and a sphere at eight segments is 224 vertices — the cost is real and it is paid
per station per frame. Nothing measured a frame-time change; the map draws thirty-odd solids.

**Every capture with a station in it is stale**, which is sixteen of the twenty-three. That is the
fourth time in two days the map's lighting has invalidated the same set, and it is the argument for
doing this kind of change in one pass rather than one tone at a time.

**ADR-106's open question is answered in the direction it did not expect.** It asked whether the
diffuse ramp should shrink to three; the measurement says four bands are contiguous at every size
the framing produces, and the owner's reading of the unchanged build — "two or three shades" — is
what a flat dark side looks like. The question is closed by going the other way.

**The dark side is now two bands and the rim, which is three tones on the half of the ball the light
does not reach.** If anything further is added there it will be competing with a 1.3-pixel band, and
the next tone has to clear the same isolated-pixel bar rather than an argument.

**`FlatRampFor` passes the new tone rather than switching it off**, unlike the rim and the glint. A
step down the light is not a curved-surface effect: a column face that lands past the terminator
should be the same colour a sphere's does, and switching it off would have made a stem's dark face
jump from grazed to shadow where the ball beside it steps.

## What this changes elsewhere

- **Design/:** `UI/DESIGN-GUIDELINES.md` §Map (the ramp and its measured shares), and sixteen
  captures. Done in this commit.
- **Code:** `NeuronClient/Color.h` (`ColorRamp::quarterLit`), `LockstepClient/DesignTokens.h`
  (`STATION_QUARTER_SHADE`, `QuarterLit`, both ramps, the static asserts),
  `NeuronClient/MeshRenderer.{h,cpp}` (the vertex and its three writers),
  `NeuronClient/MeshBackend.cpp` (an eighth input element),
  `NeuronClient/Shaders/MeshVS.hlsl` and `MeshPS.hlsl`. Done, built and run.
- **Tests:** `LockstepTests/StationBallTests` (the ramp climbs through the new step and it is
  opaque), `NeuronClientTests/MeshRendererTests` (six distinguishable tones).
- **AGENTS.md:** nothing.

## Open questions

**Whether the ramp is now finished.** Four diffuse bands, a silhouette and a glint is six tones on a
solid that is thirty pixels across. The next one has to beat a 1.3-pixel band on the isolated-pixel
count, and there is no obvious candidate left on the lit side.

**The dither, still on ADR-106's stated condition** of r≈25. The framing gives 16.5 at most, and the
zoom reaches 2.5× — but ADR-106 also said "not by switching it on with zoom", and that stands.

**Whether the far ball wants the band at all.** At r=10.6 it is 1.3 pixels and contiguous, which is
why it was kept; nobody has asked whether a player reading the map at that size sees it as shading
or as an artefact of the silhouette beside it. A tone that is correct and invisible costs a byte a
vertex and nothing else, so this is a question about honesty rather than about pixels.

**The fill light**, unchanged from ADR-105 and still carrying its number: anything proposing one has
to beat 7% of the ball at the opening framing without swallowing the shadow a quarter turn round.
