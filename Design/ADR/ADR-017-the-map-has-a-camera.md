# ADR-017 — The map has a real camera, and the authored projection is gone

**Status:** Accepted — reaffirmed by ADR-034 (2026-09-11) against `Design/UI/DESIGN-GUIDELINES.md`, which restates the authored projection this ADR replaced. That formula describes the mockups; the camera is the map.

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10. ADR-016's turntable was tried and rejected — "it just doesn't feel like a camera" — and the owner chose the orbit camera that ADR-016 had recorded as its rejected alternative.
**Supersedes:** ADR-016

---

## Context

ADR-016 made the map rotate by spinning the galaxy's coordinates on a fixed, authored curve. It
preserved the reference look exactly and cost almost nothing, and it was the option this session
recommended. In use it was wrong, and the owner's diagnosis was the useful one: not a mis-tuned
parameter, but the wrong model. A turntable spins a picture. It cannot change the angle you look
from, the grid under it cannot turn because a finite rectangle of grid sweeps its own corners
through the pane, and the depth clamp bunches far systems against the horizon. Each of those is a
symptom of the same thing — there is no viewpoint in the model to move.

The obstacle was never difficulty; it was what replacing the projection costs.
`Design/Screens/README.md` specifies the projection as the spec (`d = y/560`, `s = 0.5 + 0.65d`,
`sy = 60 + 440(0.3d + 0.7d²)`) while calling the map geometry illustrative. That curve is a
drawing decision, not an approximation of a perspective matrix: its scale ramps 0.5 to 1.15 across
the plane, a 2.3× convergence that no comfortable lens produces. Replacing it means the map stops
matching the reference at every angle, including the one the screen opens at.

## Options considered

### A. Keep the turntable and tune it

Widen the yaw range, spin the grid on a bigger patch, soften the depth clamp. Cheapest, and it
keeps the reference framing exact.

Rejected because none of it addresses the complaint. The tilt still cannot change, and a grid
patch large enough not to show its corners has to be so large that the clamp bunches all of it.
Tuning a turntable produces a better turntable.

### B. A perspective orbit camera

An eye position on a sphere around the galaxy, a look direction, a field of view. Drag horizontally
to orbit, vertically to raise and lower the eye. Near systems are genuinely larger, lanes converge,
and the ground plane turns because it is part of the world rather than a graticule drawn on the
screen.

The costs are real and there are three. The reference look goes, as above. **Depth sorting becomes
necessary**: with a fixed viewpoint the authored draw order was correct in every frame and nothing
had to decide it; an orbiting camera changes what is in front of what. And **the map can now draw
outside its pane** — a projected label or lane can land anywhere on the screen, which the authored
curve made impossible by construction.

### C. Orthographic orbit

Same camera, no perspective divide. Keeps sizes constant, which suits the flat 8px labels and is
common for strategy maps.

Rejected by the owner in favour of perspective. It is worth recording why perspective is also the
better fit: the authored curve *scaled with depth*, so a projection that does not is further from
the reference than one that does, not closer.

## Decision

**B.** `NeuronClient::OrbitCamera` is a perspective camera that orbits a target;
`Frontier::MapView` owns one, maps the handoff's 800×560 design space onto the ground plane, and
frames the galaxy. `MapProjection` is deleted.

**The camera lives in `NeuronClient`.** It is generic maths with no game vocabulary in it, and
putting it there makes it reachable from a test suite — which closes the specific gap ADR-014
recorded, where the projection was untestable because it lived in the executable. Eight tests now
cover the axes, the pitch clamp, the orbit invariants, the perspective ratio and the
behind-the-eye case.

**There is no matrix.** Every point is projected on the CPU, because the interface is drawn as
screen-pixel triangles (ADR-014); a 4×4 would be built and then used one point at a time. Working
in view space directly is the same arithmetic with the row-versus-column, depth-range and
handedness conventions all removed. If a vertex shader ever needs this camera, that is when it
grows a matrix.

**The design space survives.** The graph is still authored in the handoff's 800×560 coordinates —
that is what the README specifies and what every fixture position was measured in. `MapView` is
the single place they become world coordinates:
`(designX, designY) -> (designX - 400, 0, designY - 280)`, one design unit to one world unit,
plane at y = 0. Height is the axis design space did not have, and it is what stems rise along.

**Framing is computed, not tuned.** `MapView::FrameContent` aims at the middle of the galaxy and
solves the distance that fits it. The fixture's galaxy is illustrative and the generator will make
a different one every match, so a hand-picked distance would frame this sample and no other. It
fits the content as a **flat disc, not a ball**: a galaxy is `radius` wide from every yaw but only
`radius · sin(pitch)` tall. Bounding it with a sphere was the first attempt and framed the map
about a quarter smaller than it needed to be, all of it wasted above and below. The distance is
solved once, against the steepest pitch the camera can reach, so it never re-fits while the player
orbits — a camera that dollied in and out as you tilted it would be its own kind of wrong.

**Ground circles are projected polygons.** Shadows and the sealed region are circles lying on the
plane, emitted as projected rings rather than screen-space ellipses. What shape they make is then
the camera's business: round from overhead, a sliver from low down, and nothing in the drawing code
has to know which.

**Systems and fleets are sorted back to front** before drawing. This renderer has no depth buffer
for interface geometry, so painter's order is the whole of its occlusion model.

**The map is drawn first and the rails are painted over it.** That is what confines the map's
geometry to its pane now that a camera can put it anywhere. Text is a separate pass and cannot be
covered that way, so `FontRenderer` gained a clip rectangle that drops whole glyphs falling outside
it — whole, because a glyph cut down the middle is two lit columns of nothing on an 8×8 font.

**Both drag axes are inverted**, so the gesture grabs the world rather than the viewpoint: drag
right and the galaxy goes right, drag up and it tips away so you see more of it from above. The
other vertical sign was tried and is wrong the moment you use it — the view flattens towards the
horizon when everything about the gesture says it should rise.

**`ResetView()` puts the camera back.** ADR-016 left "no way back to the authored view" as an open
question; with a camera it is one call, and hunting for the opening framing by eye is not a thing
to ask of a player.

## Consequences

**What this makes easy.** Looking at the galaxy from anywhere. The far side of the map can be
brought to the front, a contested cluster can be looked at from above to see which lanes actually
touch it, and the sealed region can be viewed along its own lanes. Hit testing follows for free,
because the tappable rectangles are recorded from projected positions as they are drawn (ADR-014).

**What this makes hard.** The reference framing is now approximate rather than exact, and every
measured figure in ADR-014 taken at the authored view is approximate with it. The opening view is
close — same orientation, same systems in the same rough places — but a lens does not reproduce a
drawing.

**What it costs.** A projection per point per frame, a sort of a dozen drawables, and a clip test
per glyph. None measured; none plausibly measurable at this scale.

**A new failure mode.** Geometry can leave the pane. The rails cover it and the text clips itself,
but a future pane that is not opaque, or a map overlay drawn after the rails, would show it again.

**What it forecloses.** Nothing. Pan, zoom and a reset gesture are all straightforward against a
camera, and none of them were against a curve.

## Verification

Measured on 2026-09-10 from the running executable:

- Drag reaches the camera and moves it: instrumented counters recorded 134 drags in a session, with
  yaw at 3.07 rad (176°, the far side of the galaxy) and pitch at 0.49.
- The picture is correct at every angle tried. Near-overhead (pitch 1.37) the sealed region and the
  system shadows are round and the graph is seen from above; at a low angle (pitch 0.49, yaw 3.07)
  Vesk and Idris have swung to the right and the region to the left, which is the opposite side of
  the map. Lanes converge, nearer systems are larger, and nothing draws through anything it is
  behind.
- The map stays inside its pane at every angle: rails paint over its geometry and its labels clip.
- Fifty-six tests pass, including eight new ones for the camera.

**One thing was not verified end to end today.** Synthetic pointer input became unreliable in this
environment partway through the session — the same script that had driven the turntable stopped
delivering, with the application confirming `EnableMouseInPointer` succeeded and its window
foreground and under the cursor. It later delivered again, which is how the drag counts above were
obtained. The drag path is covered by unit tests either way; what could not be done was a single
clean scripted gesture from press to release with a screenshot at each end.

## What this changes elsewhere

- **Code:** `NeuronClient/OrbitCamera.{h,cpp}` and `FrontierOutpost/MapView.h` are new.
  `FrontierOutpost/MapProjection.h` is deleted. `MainPage`'s map pass is rewritten around the
  camera, gains depth sorting and `DrawGroundCircle`, and draws before the rails.
  `NeuronClient/FontRenderer` gains a clip rectangle.
- **Design/Screens/README.md:** the "Interactions" line is amended again — the perspective is not
  fixed, and the projection given there is no longer what the client uses. This ADR is the
  authority for both.
- **AGENTS.md:** no change.
- **Design/:** ADR-016 superseded. ADR-014 stands; this replaces the map's projection, not the
  interface layer it describes.

## Open questions

Whether the default pitch of 0.62 rad is right. It was picked to sit near the reference's apparent
angle, and the reference was drawn rather than photographed.

Pan and zoom, which the README asks for and neither of which exists. `PointerInput` already
produces zoom steps that nothing consumes, and a camera makes both straightforward.

Whether `ResetView` should be reachable from the interface. It exists and nothing calls it; a
double-tap on the map is the obvious binding and was not added without being asked for.

Whether the sort should include lanes. They are drawn on the plane before everything that stands on
it, which is correct while everything else is above the plane, and would stop being correct the day
something is drawn below it.
