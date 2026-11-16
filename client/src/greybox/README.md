# Readability greybox

A client-only harness for the Stage 1 gate "angled-view targeting stays readable" (design §5 and
§9). It needs no server and no asset files. It exists to be thrown away once the gate is decided.

Its default settings are the design basis for the camera (design §5): orthographic at 33°, look
around while held, wheel zoom, centring on a clicked object, a star backdrop and no plane cues.
The other settings remain for comparison.

```sh
cd client
npm run dev        # then open http://localhost:5173/greybox.html
```

## What it shows

Twelve ships (the tester, three allies, eight hostiles) fly seeded routes and exchange lock-on
fire in one of four scenes: open crossing traffic, a fight around the reference scene's largest
asteroid, a fight around a proxy of the home station, and ships heading toward and away from the
camera. The tester steers one ship with W A S D or the arrow keys.

The station scene shows one of three stations, by scale against the Stage 1 station's 14.6 m:

- **×1**, the Stage 1 station the game draws today: a dozen boxes and cylinders from
  `create_scene.py`, with its landing apron. The baseline.
- **×2.2**, the default: the M9 ring as `Design/Art/station/exterior` authors it (32.4 m across),
  about fifty boxes and cylinders from `model_station.py` and `refine_station.py`. Its ring
  leaves a gap for the hangar; the cheeks, roof and portal frame wall and cover an 8 m by 3.5 m
  mouth, and the contract's 30 m lane beyond it is clear.
- **×3**, the same ring scaled by 3/2.2, heights included.

The traffic moves out as far as the station grows past the Stage 1 station, so it passes around
and in front of the station at the same distance from its edge, and the tester starts that much
further out toward the camera. Around the ring, two hostiles (H1, H5) and an ally (A1) fly in
along the lane, turn round under the hangar roof and fly out again, so trials and the hidden-time
measure include ships in and near the mouth.

The panel changes one variable at a time; every setting is also a URL parameter, so a
configuration can be shared as a link.

| Setting           | Values                                                         | Tests                                                                         |
| ----------------- | -------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Station scale     | ×1, ×2.2, ×3                                                   | Which station size keeps ships readable at the dock (plan 9.1)                |
| Hangar mouth      | toward the site, toward the camera, side-on, away              | Whether the ring hides ships with its mouth turned each way                   |
| Camera elevation  | 33°, 40°, 45°, 50°                                             | Whether the current 33° view should stand                                     |
| Projection        | orthographic, narrow perspective (20°), wide perspective (45°) | Whether the view needs depth cues that orthographic cannot give               |
| Look around       | off, hold, free                                                | Whether the tester may orbit the camera, and whether it springs back          |
| View width        | 40, 55, 73 m                                                   | How small a ship may get before it stops being selectable                     |
| Plane cues        | on, off                                                        | Markers under ships, range rings and drop lines                               |
| Behind structures | none, silhouette, fade                                         | The two occlusion rules the design considered                                 |
| Ship heights      | flat, varied                                                   | Design §5's rule that combat ships render at one height                       |
| Colour vision     | normal, grey, protan, deutan                                   | Whether allegiance survives without colour                                    |
| Backdrop          | stars, dark, bright                                            | Sense of space when the camera turns; projectile contrast on the flat colours |

## Looking around

The design fixes one view direction (§5). The look-around setting tests relaxing that. With
**hold**, the right mouse button, or Q and E, orbits the camera around the tester's ship, and the
view springs back to the tactical view on release. With **free** the view stays where it is left
and R resets it. Each trial records the camera's yaw and elevation when it was answered, so a run
can show whether a turned view costs heading or range judgement. Steering is relative to the
ship, so it is unaffected by where the camera points.

Outside a measured run, with look-around on, clicking a ship or a structure centres the camera
on it, so the orbit turns around that object instead of the tester's ship. Any steering input,
clicking empty space, R, or starting a run takes the camera back to the tester's ship: a ship
that is steered directly should not be flown while it is off screen.

The mouse wheel zooms in every look mode, between 12 and 140 metres of visible width, starting
from the configured view width; the design already allows zoom within the fixed view. Zoom stays
where it is left, R resets it along with the view direction, and each trial records the width it
was answered at.

All three projections show the same width at the tester's ship, so on-screen ship size there is
comparable between them.

The default backdrop is a seeded star field on near-black, drawn at infinity so that it turns
with the camera and ignores zoom. It is darker than the style guide's background baseline, which
the `dark` backdrop still shows as a flat colour.

## Comparing station scales (plan 9.1)

The mouth faces the site in play, the way ships launch. Both of frontier-sim's sites lie along
+X, so **toward the site** is the default: the mouth then points to the lower right of the
resting view. The other facings show what a site in another direction would do: toward the
camera, side-on (to the right of the screen), and away from the camera, where the hangar's roof
and cheeks stand between the camera and the lane. The Stage 1 station ignores the facing.

Run the greybox block once per station, as the playtest script runs it per vision mode: three
runs of eight trials, in normal, grey and one colour-deficient vision, at the default settings
otherwise.

```text
/greybox.html?scene=station&scale=1&vision=normal         the baseline; then grey, deutan
/greybox.html?scene=station&scale=2.2&vision=normal       the ring as authored; then grey, deutan
/greybox.html?scene=station&scale=3&vision=normal         the larger ring; then grey, deutan
/greybox.html?scene=station&scale=2.2&facing=away         a facing to compare, at a chosen scale
```

A link without `scale` runs the ring at ×2.2, so the playtest script's links now do too; add
`scale=1` for the station the game draws today. Each download's name carries its station, for
example `greybox-station-2.2x-site-33deg.json`. The report reads the scene, scale and facing from
each file and gives gate 2's measures for each station and vision mode apart, with a verdict per
station, so results from different stations are never pooled:

```sh
cargo run --release -p frontier-server -- report greybox/*.json
```

The gate's own verdict still takes every file passed, so to judge one scale against gate 2's
thresholds, read its row in the table by setting, or pass only its files. Files from before
the scale was recorded count as the Stage 1 station.

## What it measures

Space starts a run of three kinds of trial, shuffled:

- **Select:** a hostile is marked briefly; click it. Records time, wrong ship, and clicks on nothing.
- **Heading:** the scene freezes and one ship is marked; click where it is heading. Correct within
  22.5°.
- **Nearer:** the scene freezes and two hostiles are marked; choose the one nearer your ship.

It also records, without the tester, how long any ship's centre stays behind a structure. Results
appear in the panel and download as JSON, including the configuration and every trial.

Hull shapes and tones, signal colours and the silhouette rule come from
`src/view/readability.ts`, which the game's expedition view draws with too. A run here measures
the rules the game uses, not a copy of them.

## Limits

- The limits shown beside results are the review's starting hypotheses. The design leaves gate
  thresholds and cohort sizes open.
- Ships are extruded outlines at the starter frigate's dimensions, and each station is boxes and
  cylinders bounding its art's parts: trim, windows and stencils are inside them, and the ring's
  tells are left out because what stands on them depends on the outpost. The harness loads no
  GLB.
- The Stage 1 station's landing deck is assumed to lie on the gameplay plane. The ring's hangar
  deck and mouth are authored at its plane.
- Ships fly through structures: nothing here collides. The ring's routes keep ship centres out
  of it on the plane.
- Steering, weapons and NPC routes are placeholders written for this harness. The real rules
  belong in `frontier-sim`.
- Colour-vision modes are approximate simulations, not a substitute for testers.
- Mouse and keyboard only.
