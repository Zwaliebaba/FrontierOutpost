# ADR-103 — Height is production, and the ball is lit in the shader

**Status:** Accepted — **its tone count and its light direction are amended by ADR-104 (2026-09-14)**: a lit surface is one of three authored tones rather than two, and the galaxy's light is raked across the opening framing rather than sitting behind the eye. Everything else here stands.

**Date:** 2026-09-14
**Decided by:** Owner decision, 2026-09-14, by instructing the map's stations to be converted to the *"height means something"* treatment (mockup option 1f) and station lighting to move off the CPU into the shader. This ADR records what that decision settles and what it rejects.
**Supersedes:** — (ADR-012's two-tone rule is taken up again, for a curved surface; ADR-017's "there is no matrix" is revised, not reversed.)

---

## Context

Since ADR-017 the map has been a ground plane seen through a real orbit camera, and every system
on it has been a stem of one of two fixed heights — 20 world units, 30 for a capital — with a flat
disc on top drawn as a screen-space ellipse and an owner-tinted puddle under it. Height was the
axis design space did not have and the camera gave the map for free (ADR-017, "Height is the axis
design space did not have, and it is what stems rise along"), and nothing was said with it.
Meanwhile the one number a player weighs a system by — what it pays a tick — was on the digest as
`Production +13` and nowhere on the board.

The disc was shaded by nothing. It was the owner's colour, flat, and the camera turning around it
changed its position and its size and nothing about its surface, which is part of why the map still
read as a diagram seen from an angle rather than a place seen from somewhere.

Three constraints bind. **Every face is one of two authored tones** (ADR-002, ADR-012): a lit
surface is a choice between two colours somebody named, never a value between them, and ADR-011
and ADR-014 forbid a colour nobody chose on the screen. **The interface is two renderers whose
vertex is two floats and a colour** (ADR-014, ADR-075): `ShapeRenderer` is canvas-pixel geometry
and every page and every test depends on that shape. And **the camera had no matrix** (ADR-017):
"if a vertex shader ever needs this camera, that is when it grows a matrix".

The mesh pass that existed once — `MeshRenderer`, `MeshVS`, `MeshPS`, deleted by ADR-015 with the
MVP-01 scene — did per-face two-tone lighting on the vertex shader with `nointerpolation`, against
a fixed light in `MeshRenderer.h`. It is the reading list for this decision, not its starting point.

## Options considered

### What the axis carries

**A. Nothing, as now.** Two heights, kind only. Rejected by the owner: a third axis that says
nothing is a third axis wasted, and the number the board most needs is already on the wire as of
this change (`SnapshotSystem::production`).

**B. Production, three ways.** The stem's height, the footprint's radius and a number under the
foot are all the same fact. Three rather than one because they are read at three distances: the
height at a glance across the board, the footprint when comparing neighbours, the number when the
answer has to be exact. Taken. The scale is 14 + 3 per credit a tick so that a system paying the
base (two credits) stands exactly where every system stood before, a capital never stands lower
than its old 30, and a zero — a system nobody holds, or a board the server has not priced —
stands at the old plain height rather than lying on the ground. Every one of those is a pure
function (`StemHeightFor`, `FootprintRadiusFor`) with a test.

### Where the ball's normal lives

**A. On `ShapeRenderer::ShapeVertex`.** Add three floats and a second colour to the interface
vertex and draw the ball through the shape pass. Rejected: every rectangle, glyph box and lane on
the screen would carry a normal it does not have, the shape pass would need depth and a matrix it
has no use for, and the pages and tests that depend on the vertex being two floats and a colour
would all move for the sake of forty-one balls.

**B. A sibling recorder and backend.** `MeshRenderer` records world-space triangles with a normal
and two tones; `MeshBackend` is the one new file that names D3D12, with the same per-frame upload
slices as `ShapeBackend`, back faces culled, depth tested and written, no blending. The split is
the one ADR-075 already made — the recorder names no API, so a headless test can tessellate a
sphere and count its triangles. Taken. It costs a third recorder in every page signature that
draws the world and a third `BeginFrame` in every headless test; that was the whole cost.

### How the ball is lit

**A. A Lambert ramp.** `color * saturate(dot(n, l))`, the conventional answer, smooth and cheap.
Rejected by the constraint, not by taste: it produces a continuum of colours between the owner's
and black, every one of them a colour nobody chose (ADR-011, ADR-014), and it is exactly the
arithmetic ADR-002 spent its length keeping out of the pixel.

**B. Two tones, chosen per face.** ADR-012 as it was: the vertex shader decides once per face from
a `nointerpolation` normal, the pixel shader has no arithmetic. On a twelve-by-eight sphere the
terminator is then a staircase of whole facets, and a ball that small reads as a faceted bead
rather than a lit sphere.

**C. Two tones, chosen per pixel.** The normal is interpolated across the face and the pixel
shader compares it against the light: `facing > TERMINATOR ? lit : dark`, nothing else. The GPU
decides *which* of the two tones a pixel gets — that is the lighting — and never makes a third;
the two tones arrive on the vertex, `nointerpolation`, from the CPU. The terminator is then a hard
curve through the triangles that turns as the camera orbits. Taken. What moved from ADR-012 is only
where the choice is made; what it protects is unchanged, and a capture can still be counted.

The dark tone is made once, on the CPU: `Ink::Shaded(lit)` is the lit colour moved 0.58 of the
way to the app background by `Neuron::Mix`, the one place two named colours become a third, at
authoring time, in a `constexpr` a test can read. 0.58 rather than a halving because the owner
colours are bright on a near-black ground and a dark side that stayed vivid read as a second owner.
The light is one direction in world space, normalize(−0.45, 0.60, 0.65), fixed to the galaxy and
not to the eye: orbit the camera and the lit side of every ball turns with the world.

### Where the camera meets the shader

**A. Transform on the CPU, upload clip-space vertices.** Keep ADR-017's "no matrix" by projecting
each sphere vertex with `Project` and handing the backend finished clip coordinates. Rejected: it
moves twenty-four thousand projections a frame onto the CPU to avoid writing sixteen floats, and it
gives up the depth test, since `Project` returns a distance and not a clip-space z.

**B. The camera grows a matrix.** `OrbitCamera::ViewProjection()` — row-major, right-handed, y up,
D3D 0..1 depth, built from the same basis, eye, field of view and viewport `Project` uses, and
neither derived from the other. Taken, on the day ADR-017 said it would be. What keeps the two
statements one camera is a test: three points off every axis at nine orientations, through both,
landing on the same pixel to a hundredth. A ball placed by the matrix and a label placed by
`Project` therefore cannot drift apart.

### Where the balls land in the frame

The balls must sit over the ground, the stems and the shadows and under the rings, the arrowheads
and the badges. A second `ShapeRenderer` would need a second upload heap; a second page-level call
would draw every station twice. `ShapeRenderer::EndLayer` instead marks a boundary the next take
stops at, so `DrawMap` records the ground half of every station and fleet, marks, then the overlay
half, and the composition root drains shapes, meshes, shapes, text. A page that never marks is
drained exactly as before. The balls are depth-tested against nothing but other balls; every flat
thing still layers by draw order, which means a far system's ring is drawn over a near ball. That
is the cost of mixing two occlusion models and it is accepted; the alternative puts the rings under
the balls they belong to.

## Decision

**Every system is a lit ball on a stem standing on the ground plane, and the stem's height is the
system's production.** The footprint on the plane is a dashed ring whose radius is the same number,
a rung marks every ten world units of stem, and the yield is written under the foot in the owner's
colour when no label is already there. The ball is shaded by one fixed world-space light into
exactly two tones — the owner's colour and `Ink::Shaded` of it — chosen per pixel in `MeshPS` and
never mixed. Ownership stays colour-only: every owner's station is the same shape. Labels, the
contested and custodian rings, the focus spotlight, the capital halo, the lanes, the fleets and the
sealed region are unchanged.

The ball is recorded by `NeuronClient::MeshRenderer` and drawn by `MeshBackend`, the one pass in
this renderer that tests and writes depth, between two shape layers separated by
`ShapeRenderer::EndLayer`. `OrbitCamera::ViewProjection()` is the camera as a matrix, held equal to
`Project` by test. `SnapshotSystem::production` carries the yield, computed by
`TickResolver::ProductionOf` — the one statement of what a system pays, which `Produce` also earns
by — and `SystemNode::production` is where the map reads it.

## Consequences

**What this makes easy.** Reading the board. The richest system in a cluster is the tallest thing
in it before any label is read, and the same number is on the plane and under the foot for a
closer look. Anything else that wants to stand in the world — a fleet as a solid, a building as
an octahedron (`MeshRenderer::Octahedron` exists and nothing uses it) — is a call on a recorder
that already draws through the camera with depth.

**What this makes hard.** Two occlusion models on one screen. The balls sort among themselves by
depth and everything flat sorts by draw order, so a ring can be drawn over a nearer ball. And the
picture is no longer checkable by "six colours on screen": a ball is two tones per owner, and
what a capture can say is that its interior holds exactly those two. Measured on 2026-09-14 from
the running client (Torvald's ball, 26×26 pixels): 426 pixels of `94,196,255` and 55 of
`46,90,119`, which is `Ink::Shaded(BLUE)` to the byte, and nothing else inside the silhouette.

**What it costs.** A third recorder in every world-drawing signature and every headless test. A
12×8 sphere is 504 vertices (two polar bands of triangles, six of quads); a fully known
twelve-player galaxy is forty-nine of them, and `MeshRenderer::MAX_VERTICES_PER_FRAME` is 32,768,
which is sixty-five. Neither the extra draw nor the extra upload was measured, for the reason
ADR-014 gives: at this scale neither is plausibly measurable. One more D3D12 pipeline, one more
pair of compiled shaders, and a 3-megabyte upload heap.

**A wire change.** `SnapshotSystem` gained a field. The client and the server are one executable,
so there is no version to bump; a remembered system reports what its remembered levels were worth
and not what a custodian's spoils would halve, because the fog does not remember that.

**What it forecloses.** Nothing that was wanted. A third tone is still a new ADR, because there is
still nowhere in this design to put one — and that is deliberate.

## Verification

Measured on 2026-09-14, Debug|x64, from the running client at `--scale 1` joined to a `--serve
--phase0 --tick 12 --bots 5` peer as seat `bravo`:

- Torvald (+10) stands visibly taller than Xerev (+2) and Orrick (+6); each stem carries its rungs;
  each foot carries its yield; the unheld Hollis is a muted grey ball, not a white one.
- The ball is exactly two tones, counted above, and the dark tone is the token's value.
- The terminator turns with the camera. A second capture at an authored yaw of 1.0 radian — a
  temporary change to `MapView::DEFAULT_YAW_RADIANS`, reverted and rebuilt afterwards — darkens
  46% of Torvald's ball (218 of 477 pixels) and 36% of Orrick's, where the opening view darkens
  11% (55 of 481); a CPU rendering of the same light against the same two cameras predicts 12%
  and 43%. The light is fixed to the galaxy, not to the eye. (The first attempt at this
  measurement compared two captures of the *same* build, because a rehearsal server still held
  the executable and the relink failed silently behind a filter that did not match `fatal error`;
  it is recorded here so the next session checks the executable's timestamp before believing a
  rebuilt picture.)
- A rehearsal with two overlapping clip-space balls, before the map used the pass, confirmed the
  depth test (every pixel of the overlap was the nearer ball's) and the winding (the lit cap
  includes the disc centre, which an inside-out ball cannot show).
- All five suites pass: 109 in `NeuronClientTests` (sphere winding and normals, the octahedron's
  faces, the layer boundary, `Mix`, the matrix against `Project`), 284 in `LockstepTests` (the
  height rules, every ball on a played board lit brighter than shaded, the world in two layers),
  and the snapshot round trip carries the yield.

Not verified, because the desktop was locked and no tap could be scripted: the focus ring and the
custodian ring over a ball, and the eleven captures that need a finger. Their code paths are the
ones that existed, moved whole into the overlay half, and the two-layer split is asserted by test.

## What this changes elsewhere

- **Code:** `NeuronClient/MeshRenderer.{h,cpp}`, `MeshBackend.{h,cpp}`, `Shaders/Mesh{VS,PS}.hlsl`
  are new. `ShapeRenderer` gains `EndLayer`; `OrbitCamera` gains `ViewProjection` and its
  near/far constants; `SceneTarget::DEPTH_FORMAT` is public; `Color.h` gains `Mix`.
  `LockstepClient/MapRender` draws stations in two halves around the boundary and frames to the
  tallest stem; `MainPage::DrawWorld` and `DrawMap` take a `MeshRenderer`; `DesignTokens.h` holds
  the station's shade and alphas. `GameLogic`: `TickResolver::ProductionOf`,
  `SnapshotSystem::production`; `Lockstep/SnapshotView` fills `SystemNode::production`.
- **Design/:** `UI/DESIGN-GUIDELINES.md` "Map" describes a station as drawn. ADR-012's status line
  records that its rule is taken up here; ADR-017's records that the camera has a matrix now.
- **AGENTS.md:** no change. R12's "the meshes carry no textures" is true again in the present
  tense.
- **Captures:** the no-finger captures in `Design/UI/screens` that show the map are retaken from
  this build; the ones that need a tap are not, and say so above.

## Open questions

Whether the rings should be depth-tested too. Drawing them as meshes — a torus, or a flat ring
with a normal — would put a far system's ring behind a near ball, at the cost of a second kind of
thing in the mesh pass and a ring that is no longer a screen-pixel line.

Whether the yield under the foot should consult the lanes as the labels do. It does not, because a
station's foot is where its lanes meet and a number a lane runs under is still a number; if it
proves illegible on a crowded board, `LabelField::TryPlace` is where that changes.

Whether a remembered system should remember its spoils. `SeenSystem` does not carry `halfYield`,
so a remembered custodian's system is reported at full yield; carrying it is a `Match` state change
and a hash change, which this ADR did not want to make for a number the fog is already wrong about.

What else stands in the world. `Octahedron` is written and tested and draws nothing; a building, a
fleet, a site pin are all candidates, and each is a decision about what a shape says.
