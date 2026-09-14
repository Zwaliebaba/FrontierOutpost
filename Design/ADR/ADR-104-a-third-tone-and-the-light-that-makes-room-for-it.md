# ADR-104 — A third authored tone, and the light that makes room for it

**Status:** Accepted — **its tone count is amended by ADR-105 (2026-09-14)**: a lit surface is one of four authored tones, the fourth driven by a second threshold on the key light rather than by the fill light this ADR left open. Its argument, that the count is not the principle, is what ADR-105 applies again.

**Date:** 2026-09-14
**Decided by:** Owner decision, 2026-09-14. The owner asked for "two tones and nothing between them" to be challenged — *not to add textures, but make it more sophisticated. What would be ideal?* — and then approved building the answer after a prototype measured what it was worth.
**Supersedes:** — It **amends ADR-103**, whose tone count and light direction it revises. Everything else in ADR-103 — height is production, the mesh recorder, the camera's matrix, the layer split — stands.

---

## Context

Every lit surface in this renderer has been one of exactly two authored colours since ADR-002, and
the reason has been restated three times without being re-examined. It is worth separating what
that rule protects, because only one of the three is load-bearing.

**Authorship** — every pixel is a colour somebody named — is the real value. It is what stops the
screen drifting into mud and it is the whole of the legacy look. **Checkability** — a capture holds
a known finite set of values — is a testing convenience, and ADR-011 already conceded it "got
genuinely weaker" when the palette went. **Impossibility** — a third tone *could not exist* — was
true under ADR-001's index target and has been gone since ADR-011; it has been convention and
review ever since.

**The number two is none of those.** It is EGA's hardware. ADR-002 chose it because index `n` and
index `n + 8` were the dark and bright halves of one hue, so "brighten this face" was an addition
and cost nothing; its own text says so. ADR-012 carried the number across to a world with no
palette, where that argument no longer applied, and ADR-103 carried it again onto a curved surface.
Three ADRs of conservatism, each inheriting a count from a machine none of this code runs on.

What has changed since ADR-002 could last look at this: **there is now an interpolated normal at
every pixel** (ADR-103). ADR-002 had one face normal marked `nointerpolation` and no per-pixel
surface coordinate at all. Several things it ruled out were ruled out by that absence rather than
by principle, and this ADR is the first of them.

## Options considered

### A. Keep two tones

Costs nothing and changes nothing. Rejected because it is the question, and because the measurement
below shows the two-tone picture at the opening framing was carrying an 11% dark side — which is
not a two-tone picture, it is a one-tone picture with a smudge.

### B. A Lambert ramp, or any continuous term

`color * saturate(dot(n, l))`, or a multiply, or a mix. The conventional answer.

Rejected on the invariant rather than on effort, and the reasoning is ADR-012's option B verbatim
and still correct: it puts arithmetic back on the colour, it produces a continuum of values nobody
chose (ADR-011, ADR-014), and an edit that widens two values into three would meet no objection
anywhere in the code.

### C. A third authored tone on the silhouette

The vertex carries a third packed colour. The pixel shader compares `dot(n, toViewer)` against a
threshold exactly as it already compares `dot(n, light)`, and **selects**. No arithmetic on a
colour, no value between two authored ones, nothing an edit can widen. The count goes from two to
three; the rule does not move.

A rim is the right third tone rather than, say, a mid-band: it is the largest single "this is a
solid" cue in stylised rendering, it costs one dot product, and it is a **strict superset** — pass
the same colour as the dark tone and the previous picture comes back exactly. Taken.

### D. A full authored ramp of four or five tones

The same idea with more thresholds: key, fill, mid, shadow, rim. It is where this leads and it is
recorded as the open road below, but it is not taken here. Three is enough to test whether a third
tone is right at all, and five is a real authoring change — every material states five colours
instead of two, and `OwnerColor` would have to yield a ramp.

### E. Ordered dithering between two tones

The one technique that gives apparent continuous shading while **every pixel remains a colour
somebody chose**: it interleaves named colours rather than inventing one. It respects authorship
more strictly than option C does, and it leaves checkability completely intact — a capture still
holds only palette members.

ADR-002 rejected it, and its reason was specific: a screen-space dither crawls across a surface as
the object moves, and an object-space one "is a texture this game has no way to author (R13)".
**The second half of that stopped being true with ADR-103.** An interpolated normal *is* an
object-space coordinate; hashed, it gives a stable, texture-free, R13-safe object-space dither that
sticks to the surface as the camera orbits. Not taken here — it wants a screenshot at 2× and 3×
canvas scale before anyone commits to it, because ADR-075 magnifies the canvas and a one-pixel
dither becomes a three-pixel block — but it is no longer ruled out, and the ADR that ruled it out
would not rule it out today.

### The light, which is part of this decision and not a detail under it

**ADR-002 said that in those words and this session proved it again the hard way.** ADR-103 recorded
`normalize(-0.45, 0.60, 0.65)` without checking it against the camera the map actually opens at. At
the default framing the eye sits at +z, so that light is **86% of the way behind the viewer**
(`L · eye` = +0.88) — the flattest lighting there is. It left 11% of a ball dark, all of it in the
outermost pixel or two of the limb.

The consequence only became visible when the rim was added, and it was severe: the rim band is the
outer fifth of the radius, so it repainted *every dark pixel there was*. Measured on 2026-09-14
from the running client, Torvald's ball at the opening framing:

| build | lit | dark | rim |
|---|---|---|---|
| ADR-103 as committed | 87% | 11% | — |
| third tone, ADR-103's light | 87% | **0%** | 12% |
| third tone, raked light | 60% | 18% | 22% |

A ball with no dark side and a bright outline is a sticker, not a sphere. **The third tone was not
the problem and would not have been the fix**; the light was, and the two had to be chosen together
exactly as ADR-002 warned. Four candidate directions were modelled on the CPU before any was built;
the chosen one, `normalize(-0.85, 0.45, 0.10)` (`L · eye` = +0.35), was predicted to give 58/20/21
and measured 60/18/22.

## Decision

**A lit surface is one of THREE authored colours, chosen per pixel: the tone where the light finds
it, the tone where the light misses it, and the tone on the silhouette where it turns away from the
eye.** The shader selects; it never mixes, scales or interpolates a colour, and a pixel is still a
value somebody named. `MeshVertex` carries all three, `nointerpolation`, and `MeshPS` compares
`dot(n, light)` against `TERMINATOR` (0.15, unchanged) and `dot(n, toViewer)` against
`RIM_TERMINATOR` (0.35).

**The rim only ever repaints what the light misses.** On the lit side it would be an outline drawn
around a shape the light has already described — a sticker with a border — and on the dark side it
is the light that got past the object, which is what stops a ball dying into the background where
its dark half meets it. That ordering is the whole difference between a rim light and an outline
shader, and it is why the two thresholds are combined rather than chained.

**The galaxy's light is `normalize(-0.85, 0.45, 0.10)`**, world-space and fixed to the world as
before. It is raked across the opening framing rather than sitting behind the eye.

A station's three tones are `OwnerColor`, `Ink::Shaded` of it (0.58 toward the background,
unchanged) and `Ink::Rimmed` of it (0.45 toward white). White rather than the owner's own colour,
because a rim is the light getting past the ball rather than the ball's material — and because
white keeps its distance from all twelve owner colours, so a rimmed rival is still that rival
(ADR-027). `static_assert`s hold the ramp in order, as ADR-012 held its pairs.

## Consequences

**What this makes easy.** A ball reads as a sphere. Anything else that comes to stand in this
world — a fleet as an octahedron, a building, a site pin — gets the same three-band treatment for
nothing, because the tones travel on the vertex.

And the count is no longer sacred, which is the larger consequence. A fourth tone is now the same
*kind* of change as this one rather than a challenge to a principle: add a threshold, add a packed
colour, keep the selection. The principle was never the number.

**What this makes hard, and it is the same thing ADR-002 recorded.** The picture now depends on the
light and the camera *together*, and this session is the second proof of it: moving the light
flattened the balls under a scheme that was fine, and adding a tone exposed a light that was not.
**A change to the map's default framing re-opens the light**, and anyone who changes one without
looking at the other will get exactly the picture this ADR was written to fix.

**What it costs.** Four bytes a vertex (32 → 36), one input element (4 → 5), four root constants
(20 → 24) for the eye position the rim is measured from, and one dot product and one compare per
pixel. None was measured against a frame time, for ADR-014's reason: at forty-nine balls neither is
plausibly measurable.

**The quarter-turn view is darker, and that is a real cost rather than a neutral one.** Modelled
across the orbit, the raked light gives 58/20/21 at the opening yaw, 20/53/27 a quarter turn round,
and 50/28/22 at the far side. A world-fixed light means some views are backlit; that is the point
of ADR-103's decision to fix it to the world, and it is what makes orbiting mean something. But
half a turn from the opening framing the board is now noticeably darker than it was, and if that
reads badly in play the answer is a fill light (below) rather than un-raking the key.

**Checkability survives and gets one number larger.** A capture holds three tones per owner instead
of two — still finite, still named, still countable. `LockstepTests` asserts the ramp runs
dark → lit → rim on every ball of a played board.

**What it forecloses.** Nothing that was wanted. Continuous shading, sampled textures, MSAA and any
post-process that averages neighbouring pixels are still out, and they are out on
`Design/README.md` §1's no-resampling line and the authorship rule — not on a count.

## Verification

Measured on 2026-09-14, Debug|x64, from the running client at `--scale 1` joined to a
`--serve --phase0 --tick 12 --bots 5` peer as seat `bravo`. All three builds photographed the same
board at the same tick (D4/21, digest tick 13), so the figures compare directly:

- Torvald's ball, 226 pixels: **135 lit (60%), 41 dark (18%), 50 rim (22%)**, and no fourth value
  inside the silhouette. The tones are `94,196,255`, `46,90,119` and `166,223,255` — `OwnerColor`,
  `Ink::Shaded` and `Ink::Rimmed` of it, to the byte.
- A CPU model of the same light, threshold and camera predicted 58/20/21. The model and the
  hardware agree within two points, so the behaviour is understood rather than stumbled into. The
  same model predicted 89/0/11 for the rejected build and the capture measured 87/0/12.
- The rim is a crescent on the limb the light does not reach, not a ring: it appears only where the
  dark band already was, which is what the combined threshold is for.
- All five suites pass, 613 tests, including a new assertion that every ball on a played board
  carries three opaque tones in ramp order.
- `CheckFormat`, `CheckProjectFiles` and a whole-tree `RunClangTidy` are clean.

Not verified, because the desktop was locked and no tap could be scripted: the eleven captures that
need a finger. The rim is a property of the mesh pass and does not vary with what sheet is open.

## What this changes elsewhere

- **Code:** `NeuronClient/MeshRenderer.h`'s `MeshVertex` gains `rimColor` and `View` gains
  `eyePosition`; `Sphere` and `Octahedron` take a third tone. `MeshBackend` gains an input element
  and four root constants. `Shaders/MeshVS.hlsl` passes the world position and the third tone;
  `MeshPS.hlsl` selects between three. `LockstepClient/DesignTokens.h` gains `STATION_RIM` and
  `Ink::Rimmed`; `MapRender.cpp`'s `LIGHT_DIRECTION` is raked and the station passes its rim tone.
- **Design/:** `UI/DESIGN-GUIDELINES.md`'s Map section describes three tones and the new light.
  ADR-103's status line records that its tone count and light are amended here.
- **AGENTS.md:** no change.
- **Captures:** the five no-finger captures that show the map are retaken. Four of them changed;
  `05-match-finished` re-rendered byte-identically, which is correct rather than stale — its map
  pane holds only the dialog card, the stars and the legend, no station is visible behind the card,
  and the match it photographs is deterministic (ADR-018), so the same script produces the same
  pixels.

## Open questions

**The full ramp.** Four or five authored tones — key, fill, mid, shadow, rim — is option D and is
the natural next step now that three has been built and the count has stopped being a principle.
What it needs is an answer to how a material states five colours without five literals: a
`Ramp(owner)` yielding `constexpr` steps, the way `Ink::Shaded` and `Ink::Rimmed` already do.

**Object-space dithering**, option E, which ADR-002's objection no longer reaches. It wants a
screenshot at 2× and 3× before anyone commits, because the canvas is magnified by whole numbers
(ADR-075) and a one-pixel dither becomes a three-pixel block. It may be that the block *is* the
period-correct answer; that is a thing to look at rather than to argue.

**A stepped specular.** ADR-012 forecloses "specular", but what it forecloses in its reasoning is a
*continuous* one. A thresholded lobe is one more authored tone and one more compare.

**A fill light from the opposite side.** ADR-002 and ADR-012 both left this open and it was
pointless under one threshold — it only ever made more of the ball lit. With three tones it becomes
a distinct band, and it is the answer to the darker quarter-turn view recorded above.

**Whether the raked light is raked far enough, or too far.** It was chosen from four modelled
candidates against the *opening* framing, which is the view every capture and every new player
sees. The other views were modelled and not judged by eye.
