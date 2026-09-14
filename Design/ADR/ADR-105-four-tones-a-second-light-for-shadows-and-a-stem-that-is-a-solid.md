# ADR-105 — Four tones, a second light for shadows, and a stem that is a solid

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision, 2026-09-14, choosing from the open road ADR-104 recorded: cast shadows and solid stems to build, a separate steeper direction for the shadow, and the fourth tone built now rather than left open — the last of those against this session's recommendation, which is noted below and was wrong for a reason worth keeping.
**Supersedes:** — It **amends ADR-104** (the tone count, again) and **ADR-103** (which said one light). Everything else in both stands.

---

## Context

ADR-104 took the tone count from two to three and made the point that the count was never the
principle — authorship was, and two was EGA's accident carried across three ADRs. It left four
things open. Three of them are settled here.

What it also left was a cost it named honestly: a world-fixed key light means some views are
backlit, and a quarter turn from the opening framing the board is modelled at 20% lit. And a
recommendation this session made and the owner overrode — that a fourth tone should wait, because
at 8–12 canvas pixels of radius the bands get thin. **The recommendation was wrong in an
instructive way.** It assumed a fourth band had to come from a second light; measured, the second
light was the bad idea and the fourth band was not.

## Options considered

### Where a fourth band comes from

**A. A fill light from the opposite side.** What ADR-104's open question proposed and what this
session expected to build. A second direction, a second threshold, and the band is what the fill
finds and the key misses.

Modelled on the CPU before anything was built, against the opening framing and two others, it
measured badly enough to abandon. A fill opposite the key at `normalize(0.80, 0.35, −0.25)` put
**7%** of the ball in the new band at the opening yaw — invisible — and a quarter turn round it
swallowed the shadow entirely, 52% mid against **1%** dark. Every variation tried moved the
failure rather than fixing it: a ground-bounce direction never crossed its threshold at all (0% at
every angle), and an opposite-and-below one gave 0%, 24% and 6% across three yaws. The reason is
structural rather than a matter of tuning: the region a fill lights is the region the key misses,
which is the same region the rim already owns and is the whole ball once the camera is behind the
light.

**B. A second threshold on the key light.** No new light: the diffuse term is banded twice, so a
surface squarely facing the light is one tone and a surface grazing it is another. Taken. Modelled
at `FULL_TERMINATOR` 0.50 and `GRAZE_TERMINATOR` 0.10 it puts **all four bands on the ball at every
angle** — 35/27/18/20 at the opening yaw, 6/16/52/26 a quarter turn, 27/26/26/21 at the far side —
and none of them swallows another.

This is what a four-tone cel ramp normally means, and it is the shape ADR-002 would have reached
for if EGA had given it more than a pair. The fill light stays open, now with a measurement
attached saying what it would have to beat.

### Where the shadow's direction comes from

ADR-104 raked the key light hard, which is what made cast shadows worth drawing at all — under the
old near-eye light the offset was foreshortened to nothing. It also made an honest shadow useless:
the offset is `|light.xz| / light.y` times the height, which for `normalize(−0.85, 0.45, 0.10)` is
**1.9×**. A base station at 20 units throws 38; a rich capital at 62 throws 118. Systems sit 40 to
80 units apart, so every tall station would lay its shadow across a neighbour's footprint, and the
shadow would say which neighbour a station is nearest rather than how tall it is.

**A. A separate, steeper shadow direction.** `normalize(−0.51, 0.86, 0.06)`: the same bearing
across the plane as the key, much steeper, so the offset is 0.60× the height — 12 units and 37.
Taken. It is a lie, and it is the lie every stylised renderer tells: the mismatch between where the
light visibly comes from and where the shadow falls is not readable at this scale, and what it buys
is a cue that carries the same fact the stem does.

**B. Clamp the key light's offset.** Honest direction, capped length. Rejected because past the cap
every station shares one offset, so the shadow stops saying anything about height exactly where the
stems differ most — it fails at the tall end, which is the end that matters.

**C. The key light, honestly.** Rejected above.

### What a stem is

**A. A 1px line, as before.** The same line at every depth and under every light. It cannot
converge, cannot catch the light, and says nothing except where it is.

**B. A world-space column through the mesh pass.** Taken. It is depth-tested against the balls like
everything else standing up, it converges with distance, and its faces land in different bands so
the stem has a lit side and a shaded one.

**The cross-section is a diamond and not a square, and that is not a detail.** A square column on
an axis-aligned plane presents one face to a camera at the default yaw, so the whole stem lands in
one band: measured on the first build as three pixels of a single tone, which is a flat bar with
extra steps. Turned forty-five degrees it presents two faces at different angles to the light, and
the stem reads as a solid from the moment the map opens — measured after the change as two lit
pixels beside one shadowed one.

## Decision

**A lit surface is one of FOUR authored colours, chosen per pixel by two thresholds on the key
light and one on the view:** the band the light finds squarely, the band it only grazes, the
shadow, and the silhouette it gets past. `Neuron::ColorRamp` carries them, `Ink::RampFor` derives
all four from the one colour a caller states, and the shader selects — it never mixes, scales or
interpolates. There is no fill light.

**A station's shadow falls along `SHADOW_DIRECTION`, which is not the key light**, and lands at 0.60
of the stem's height rather than 1.9 of it.

**A stem is a diamond-section column in the mesh pass**, `STEM_HALF_WIDTH` 1.2 world units, carrying
the station's own ramp. Its rungs move to the overlay layer, because a rung recorded before the mesh
pass is painted over by the column it measures — and they stop a ball's radius short of the top,
because a rung drawn where the ball is is a rung drawn on the ball.

**A flat-faced solid switches its rim off**, by `Ink::FlatRampFor` passing the shadow as the
silhouette tone — which is exactly the strict-superset escape hatch ADR-104 built. A rim is a
curved-surface effect: the shader reads a low `dot(normal, toViewer)` as "this is the limb", which
is true on a sphere and false on a column, whose far face is oblique across its whole area. Before
this existed the column's unlit face came out *brighter* than its lit one — measured, and the second
defect the pixels found rather than the eye.

## Consequences

**What this makes easy.** A ball carved into four steps instead of two, and a stem that reads as
something standing rather than a scratch. Anything else that comes to stand in this world gets both
for nothing: `Column` and `Octahedron` are general, and `RampFor` turns one owner colour into a
whole ramp so twelve owners never become forty-eight literals.

**What this makes hard.** The bands are thin — two canvas pixels each on a mid-size ball, and
ADR-075 magnifies the canvas by whole numbers, so at scale 3 they are six-pixel stripes rather than
smoothness. This session recommended against a fourth tone for that reason and the owner decided
otherwise; what the measurement shows is that the bands are *present and correct at every angle*,
which was the real question, and whether four steps read better than three at a given present scale
is a thing to look at rather than to argue. **The next tone is the one that should have to prove
itself**, and a fifth would be two-pixel bands becoming one.

**What it costs.** Eight bytes a vertex (32 → 40 since ADR-103), two input elements (4 → 6), one
extra compare per pixel, and a column's thirty vertices per station on top of the ball's. A
twelve-player galaxy is now about 26,000 vertices against `MAX_VERTICES_PER_FRAME`'s 32,768, which
is the first time that budget has been worth stating rather than assuming.

**A second light exists now, and it is not a light.** `SHADOW_DIRECTION` shades nothing; it only
says where a shadow lands. ADR-103's "one light for the whole galaxy" is still true of *lighting*,
and this is the exception that has to be read as one — a session that adds a third direction should
say which of the two kinds it is.

**What it forecloses.** Nothing that was wanted. Continuous shading, textures, MSAA and any
post-process that averages neighbouring pixels are still out, on `Design/README.md` §1's
no-resampling line and the authorship rule.

## Verification

Measured on 2026-09-14, Debug|x64, from the running client at `--scale 1` joined to a
`--serve --phase0 --tick 12 --bots 5` peer as seat `bravo`, on the same board and tick as ADR-104's
figures (D4/21, digest tick 13):

- Torvald's ball, 255 pixels: **40% lit, 23% grazed, 18% shadow, 20% silhouette**, against a CPU
  model predicting 35/27/18/20. Four bands, all present, none swallowing another.
- The four tones are `94,196,255`, `69,141,185`, `46,90,119` and `166,223,255` — the owner colour
  and `Ink::HalfLit`, `Ink::Shaded`, `Ink::Rimmed` of it, to the byte.
- The column below it reads `LLd` across its three visible pixels: two faces of the diamond, one
  lit and one shadowed. Before the diamond it read `hhh`, one tone.
- Cast shadows are visible as ellipses offset from each foot, on the side away from the light.
- All five suites pass, 614 tests, including a new one that a column stands on its foot with every
  face wound outward, and an amended assertion that a silhouette tone either lifts above the lit
  face or is exactly the shadow.
- `CheckFormat`, `CheckProjectFiles` and a whole-tree `RunClangTidy` (80 translation units) are
  clean.

One test, `NeuronCoreTests::PollingALookupDoesNotStopTheCaller`, failed once during this work and
passed on its own immediately afterwards, 43 of 43. It is a DNS-timing test and it flaked under a
concurrent clang-tidy run and a capture; it is unrelated to anything here and is recorded so the
next session does not go looking for a defect this change did not cause.

Not verified, because the desktop was locked and no tap could be scripted: the eleven captures that
need a finger.

## What this changes elsewhere

- **Code:** `NeuronClient/Color.h` gains `ColorRamp`. `MeshRenderer`'s vertex gains
  `halfLitColor`, its primitives take a ramp rather than loose colours, and `Column` is new.
  `MeshBackend` gains an input element. `MeshVS`/`MeshPS` carry and select four tones.
  `LockstepClient/DesignTokens.h` gains `STATION_HALF_SHADE`, `Ink::HalfLit`, `Ink::RampFor` and
  `Ink::FlatRampFor`. `MapRender.cpp` gains `SHADOW_DIRECTION` and `STEM_HALF_WIDTH`, offsets the
  shadow, draws the stem as a column and moves the rungs to the overlay.
- **Design/:** `UI/DESIGN-GUIDELINES.md`'s Map section describes four tones, the shadow and the
  column. ADR-104's and ADR-103's status lines record what is amended.
- **AGENTS.md:** no change.
- **Captures:** the no-finger captures that show the map are retaken.

## Open questions

**The fill light**, still — now with a number against it. Anything proposing one has to beat 7% at
the opening framing and has to not swallow the shadow at a quarter turn.

**Object-space dithering**, unchanged from ADR-104 and more interesting now that the bands are thin:
it is the one technique that buys gradation without needing band width. It still wants a capture at
`--scale 2` and `--scale 3` before anyone commits.

**A stepped specular**, unchanged from ADR-104. A spot rather than a band, so it is the one addition
that does not compete for band width.

**Whether four tones read better than three at every present scale.** The bands are two canvas
pixels; magnification makes them blockier rather than smoother. Judge it from a capture at scale 3
on a late-match board, where the balls are smallest.

**What else stands in the world.** `Octahedron` is still written, tested and drawing nothing, and
fleets are still arrowheads in the overlay layer that draw over the balls they are behind.
