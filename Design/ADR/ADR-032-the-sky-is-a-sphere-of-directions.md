# ADR-032 — The sky is a sphere of directions, and it sweeps at the focal rate

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Build session, at the owner's report that the star field looked wrong when the camera moved.

**Supersedes:** the star-field paragraph of ADR-016, which ADR-017 carried forward with a larger constant and the same mistake.

---

## Context

The owner's report was that moving the camera around the map "doesn't look good, and it should be
kind of a Skybox setup." Both halves were right, and the reason is written down in ADR-016:

> **The star field drifts against the spin**, by `STAR_PARALLAX_PIXELS_PER_RADIAN` (14) and wrapped.
> Stars are meant to be very distant, so they parallax barely at all — but not *at all* reads as a
> painted backdrop, and a little is what sells the turntable as depth.

**That reasoning conflates two different transforms.** Parallax is what you get from moving the
eye's *position*, and for something at infinity it really is zero. But orbiting the camera also
*rotates* the view, and under rotation a distant thing sweeps across the frame at the **full
focal-length rate** — the fastest anything moves, not the slowest. What stands still under rotation
is whatever is painted on the glass.

So the intuition "stars barely move" was applied to the wrong half of the camera, and the field was
built as thirty dots in screen space slid sideways by a constant. ADR-017 replaced the turntable
with a real camera and raised the constant from 14 to 90, which changed the size of the error and
not its kind.

**Measured**, at the map's own pane (650×672) and field of view (0.70 rad):

| | measured | what the code did |
|---|---|---|
| yaw, centre of frame | 1131 px/radian | 90 px/radian |
| yaw, ±0.15 rad off centre | 1157 px/radian | 90 px/radian |
| pitch 0.20 | 958 px/radian | 90 px/radian |
| pitch 1.20, near the top of frame | 7026 px/radian | 90 px/radian |

About a tenth of the right rate, which is why it read as painted on the screen — the exact failure
ADR-016 was trying to avoid.

Three more defects followed from the representation rather than from the constant:

**A full turn did not put the sky back.** `OrbitCamera` stores yaw unwrapped, so the slide grew
without bound. One 360° orbit moved the sky 565.5 px, wrapped modulo the 800-unit design width, for
a net displacement of 565.5 px. Two turns: 331 px. The same view of the galaxy had a different sky
every time round.

**Tilting opened an empty band along the top.** The thirty stars spanned 510 px of a 672 px pane and
there was no vertical wrap, so the sky slid down and nothing replaced it: 31 px of starless pane at
minimum pitch, **146 px at maximum** — about a fifth of the map.

**A flat slide cannot have a spread.** The table above is not one number. How fast a star crosses
the frame depends on where in the frame it is, because that is what `tan` does to a lens.

## Options considered

### A. Raise the constant

Multiply 90 by about twelve and the centre of the frame is right. Nothing else is: the sky still
does not come back after a turn, still opens a band when tilted, and now opens it eleven times
faster. It also gets *more* obviously wrong, because at the correct speed the eye can see that the
stars move rigidly rather than spreading.

### B. Keep screen-space stars, add a rotation and a vertical wrap

Wrapping vertically and rotating the field about the pane's centre fixes two of the four. The spread
across the frame is still missing, and each fix is a special case bolted to a representation that
does not want them. It is also the more code of the two options.

### C. Stars as directions on a sphere, projected through the same camera

The sky stops being a backdrop and becomes part of the world — infinitely far away in it, which is a
direction rather than a place.

## Decision

**C.** `NeuronClient/Starfield` holds unit directions; `OrbitCamera::ProjectDirection` projects them.

**Every one of the four defects disappears as a consequence rather than as a fix.** The sky comes
back after a full turn because a direction is periodic. It never opens a band because a sphere has
no edge to run off. It spreads towards the corners because it goes through the same lens as
everything else. And it sweeps at the focal rate because that is what the projection does — there is
no rate constant left in the code to be wrong.

**`ProjectDirection` is not `Project` of a point a long way off**, and the distinction is worth the
method. A point at a distance has parallax, and the only way to make that vanish is to choose a
distance large enough for the arithmetic to drop it — a fudge factor whose correct value depends on
the scene and which silently stops being large enough when the scene grows. A direction has no
position: the eye cancels out of the subtraction before it is done. The implementation is `Project`
with one line removed, and a test cross-checks the two agree for a star four million units away.

**The sky is generated from a pinned seed, not authored.** R13 leaves nowhere to put a star texture,
and a hash gives the same sky on every machine and every run — which matters because a screenshot of
the map is only comparable with another one if the background is identical.

**The draw is uniform on the sphere, which needs the height sampled flat.** Drawing two angles and
calling them latitude and longitude bunches stars at the poles, and on a map that tips all the way
to overhead that is not subtle: the sky would visibly thicken as the camera rose.

## Consequences

**800 stars where there were 30**, because only the frustum's share is ever on screen — about one
part in twenty-six at this field of view. Measured across the whole tilt and a full turn, that shows
**16 stars at the emptiest angle and 43 at the fullest, averaging about thirty**, which is what the
authored field showed and what the count was chosen to reproduce. A changed field of view changes
that, and `AboutThirtyStarsAreVisibleFromAnywhere` is the test that will say so.

**Half the sphere is always behind the camera and is not drawn.** A direction behind the eye has no
projection; drawing it anyway would mirror stars into the pane a second time.

**A star at the very edge pops rather than sliding off.** `ShapeRenderer` has no clip rectangle, so
the cull tests the centre: at most a pixel of error for a dot a pixel across. Anything larger drawn
this way wants a clip rectangle instead, and this is the note that says so.

**The sky is the first thing in the pane and depth-tests against nothing.** Everything drawn after
it covers it.

**Nothing tested the old field.** That is how a sky moving at a tenth of the right rate survived two
ADRs: it looked exactly like a sky that worked. There are now nine tests, and every one of them is a
test the old field would have failed.

## What this changes elsewhere

`OrbitCamera` gains `ProjectDirection` and `InsideViewport`. `MainPage` loses the thirty literals,
the slide constant and the wrap arithmetic, and gains a `Starfield` member and one line to draw it.

**The name is reused deliberately.** `NeuronClient/Starfield` was the MVP-01 scene's hash-based
parallax field (ADR-010), deleted with that scene on 2026-09-10. This is a different thing at the
same address, and ADR-010 stays Deprecated: its argument was about an unbounded plane that scrolls
forever, and a sphere is bounded.

## Open questions

**Whether stars should twinkle or vary in brightness.** They vary in size across a 0.7–1.2 px range
and that is all. Brightness would be the better cue and needs a colour per star rather than one for
the field.

**Whether the sky should be denser near the galactic plane.** Real skies are. This one is uniform,
which is the honest default and looks like deep space rather than like anywhere in particular.

**Whether anything else wants a direction projected.** Nothing does today. If a second caller
appears — a distant nebula, a compass rose, a sun — `ProjectDirection` is already the thing it wants.
