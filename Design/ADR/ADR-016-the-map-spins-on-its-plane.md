# ADR-016 — Dragging the map spins the galaxy on its plane, and a tap is now a press and a lift

**Status:** Superseded by ADR-017 — the turntable was built, tried and rejected on 2026-09-10: it spun the picture but could not move the viewpoint, which is what "it does not feel like a camera" meant. Its tap-versus-drag decision survives unchanged in ADR-017; only the rotation model was replaced.

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10, choosing between the three rotation models offered in the session that built it.
**Supersedes:** —

---

## Context

`Design/Screens/README.md` "Interactions" says: *"Map: pan/zoom; perspective is fixed (no free
camera)."* The owner has asked for the map to rotate when it is dragged, which is a free camera by
any reading. This ADR takes that request and records what it changes.

The thing being rotated is not a camera. `MapProjection` is a hand-authored curve —
`d = y/560`, `s = 0.5 + 0.65d`, `sx = 400 + (x−400)s`, `sy = 60 + 440(0.3d + 0.7d²)` — with no eye
position, no field of view and no matrix in it. `Design/Screens/README.md` calls the map geometry
illustrative and **the projection the spec**, so whatever rotation means here, the curve is the
part that must not be casually replaced.

The second constraint is the input layer. Before today nothing on this screen could be dragged, so
`PointerInput` reported a tap on `WM_POINTERDOWN` — the moment the finger landed. A screen with a
draggable map cannot do that: at press time there is no way to know whether the finger is about to
lift or about to travel.

## Options considered

### A. Spin the content on the plane

Yaw the graph's design-space coordinates about the centre of the plane *before* the curve is
applied. The curve — and therefore the horizon, the tilt and the whole authored look — never moves.
Systems that swing towards the viewer get nearer and larger because that is what the projection
already does with depth.

It is cheap: two multiplies and two adds per point, and every property the projection was chosen
for survives untouched. At yaw zero the view is pixel-for-pixel the authored one, which is the
property that makes this safe to ship — the reference is still the reference.

Its limit is that it is a *turntable*, not a camera. The viewer's height and angle above the plane
cannot change, so the map cannot be looked at more steeply or more flatly.

### B. A real orbit camera

Replace `MapProjection` with a view and a perspective matrix: drag horizontally to yaw, vertically
to change the pitch. Genuinely three-dimensional and what a player might eventually expect.

Rejected because it throws away the thing the handoff was most specific about. The authored curve
is not an approximation of a perspective matrix — the 0.3/0.7 mix that compresses the far half of
the plane is a drawing decision, and a real projection does not reproduce it. Adopting B means the
map stops looking like the reference at *every* angle including the default, which is a much
larger change than the one that was asked for.

### C. Yaw on the plane, plus a tilt that reshapes the curve

Spin as in A, and let vertical drag move `SCALE_RANGE` and the depth mix so the plane can lie
flatter or steeper.

Rejected as the worst of both: the look is preserved only at the default tilt, so it has A's
limitation dressed up as B's freedom, and it makes two of the projection's constants into runtime
state that every measured figure in the design record was computed against.

## Decision

**A.** Dragging horizontally on the map yaws the galaxy about the centre of the plane;
`MapProjection` gains the yaw and applies it to design-space coordinates before the curve.
`YAW_RADIANS_PER_PIXEL` is 0.005, so a drag across the map pane is a little over half a turn.

**The design axes are the ground axes.** `x` runs across the plane and `y` runs into it, so
rotating the pair is a spin about the vertical. That is why this is two multiplies rather than a
matrix.

**Geometry scales by where a point ended up, not where it was authored.** A system that has swung
towards the viewer is drawn at its new depth's scale — node radius, stem height, shadow and the
region's ellipse all go through `YawedScaleAt`. Getting this wrong is the failure that would make
the rotation read as a diagram sliding around rather than as a plane turning.

**The grid does not spin.** It is the projection's graticule — a ruling of the plane, like the
graduations on a radar — and it is drawn through `ProjectUnspun`. The alternative, spinning it with
everything else, was tried on paper and rejected: the grid is a finite rectangle, so rotating it
sweeps its own corners across the pane. Held still it reads as the ground, and the rotation is
carried by the systems, the lanes and the region, which is where the eye is anyway.

**The star field drifts against the spin**, by `STAR_PARALLAX_PIXELS_PER_RADIAN` (14) and wrapped.
Stars are meant to be very distant, so they parallax barely at all — but not *at all* reads as a
painted backdrop, and a little is what sells the turntable as depth.

**Depth is clamped to the plane.** Nothing in the authored galaxy sits outside `d ∈ [0, 1]`, so at
yaw zero the clamp never fires. Spinning a wide, shallow set of systems makes it deep: the widest
point of this sample galaxy swings about 8% past the horizon at some angles, and unclamped it would
project *above* the horizon — which on a ground plane means behind the camera. Clamped, it stops at
the horizon, which is where a thing at infinity belongs. A generated galaxy that is rounder than
this hand-drawn one will not reach the clamp at all.

### A tap is now a press and a lift

`PointerInput` no longer reports a tap on `WM_POINTERDOWN`. A press is remembered; on
`WM_POINTERUP` it becomes a tap if it never travelled more than `TAP_SLOP_PIXELS` (4), and a drag
otherwise. The two are mutually exclusive by construction, so a gesture that rotates the map cannot
also open whatever it started on.

Three details are load-bearing and each has a test:

- **The movement made before the slop was crossed counts.** A quick flick would otherwise lose its
  first four pixels and the map would lag the finger.
- **A tap reports where the finger went DOWN**, not where it came up, so a tap that wobbled a pixel
  still means the thing the player aimed at.
- **A second contact cancels both**, because it is a pinch. Without that, a zoom would also spin the
  map and tap wherever the first finger landed.

**A drag belongs to whatever was under the press.** `PointerInput::Drag` carries its origin, and
`MainPage` rotates only when that origin was inside the map pane — so a drag that starts on the
orders rail never spins the galaxy, even when the finger crosses onto the map.

## Consequences

**What this makes easy.** The far side of the map can be brought to the front, which the fixed view
could not do: at yaw zero the sealed region sits behind Pell and Narth, and a drag puts it in front
of them. Hit testing follows for free, because the tappable rectangles are recorded from projected
positions as they are drawn (ADR-014) — there is no second copy of the layout to rotate.

**What this makes hard.** Nothing new, but two things stay hard: the viewer's angle above the plane
still cannot change, and there is still no pan and no zoom. The README asks for both and neither is
implemented.

**What it costs.** Two multiplies and two adds per projected point, on a screen that projects a few
hundred of them. Not measured; there is no arithmetic in which it matters.

**The tap change is a behaviour change, not an addition.** Five existing tests asserted that a tap
arrived on the press and were updated to lift the finger. Anything else that assumed press-to-act
would need the same treatment; nothing else did.

**What it forecloses.** Option B is still reachable and would supersede this. It is a bigger change
than it looks, for the reason in *Options considered*, and it should be taken because the game wants
a camera rather than because rotation was wanted.

## Verification

Measured on 2026-09-10 from the running executable:

- With a forced yaw of 0.6 rad the galaxy spins about the plane's centre: Pell and the sealed
  region come forward and grow, Idris and Vesk recede and shrink, the horizon and grid do not move.
- Dragging 280 px right from inside the map yaws by 1.4 rad (80°) and the rotation persists after
  the finger lifts. `VESK` ends up near and large, the sealed region far and small.
- Forty-eight tests pass, including seven new ones covering tap-versus-drag discrimination, the
  slop, accumulation across frames, the pinch cancel and capture loss.

## What this changes elsewhere

- **Code:** `NeuronClient/PointerInput.{h,cpp}` gains `Drag` and `TakeDrag`, and decides taps on
  the lift. `FrontierOutpost/MapProjection.h` gains the yaw, `DepthAt`'s clamp, `Yaw`,
  `YawedScaleAt` and `ProjectUnspun`. `FrontierOutpost/MainPage.{h,cpp}` holds the yaw and
  `HandleDrag`. `FrontierOutpost.cpp` takes the drag each frame.
- **Design/Screens/README.md:** the "Interactions" line is amended — the perspective is no longer
  fixed. This ADR is the authority for that.
- **AGENTS.md:** no change.
- **Design/:** nothing superseded. ADR-014 stands; this extends the map it describes.

## Open questions

**There is no way back to the authored view.** A player who spins the map is at whatever angle they
left it, and the reference framing — which every measured figure in ADR-014 was taken at — is only
reachable by dragging back to it by eye. A double-tap to reset, or a snap to the nearest eighth
turn, would fix it; neither was asked for.

Whether the yaw should persist across a tick, or across a session. It currently lives in `MainPage`
and survives both, because nothing resets it.

Pan and zoom, both of which the README asks for and neither of which exists. `PointerInput` already
produces zoom steps and nothing consumes them.
