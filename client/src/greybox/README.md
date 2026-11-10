# Readability greybox

A client-only harness for the Stage 1 gate "angled-view targeting stays readable" (design §5 and
§9). It needs no server and no asset files. It exists to be thrown away once the gate is decided.

```sh
cd client
npm run dev        # then open http://localhost:5173/greybox.html
```

## What it shows

Twelve ships (the tester, three allies, eight hostiles) fly seeded routes and exchange lock-on
fire in one of four scenes: open crossing traffic, a fight around the reference scene's largest
asteroid, a fight around a proxy of the home station, and ships heading toward and away from the
camera. The tester steers one ship with W A S D or the arrow keys.

The panel changes one variable at a time; every setting is also a URL parameter, so a
configuration can be shared as a link.

| Setting           | Values                                                         | Tests                                                                         |
| ----------------- | -------------------------------------------------------------- | ----------------------------------------------------------------------------- |
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

The mouse wheel zooms in every look mode, between 12 and 140 metres of visible width, starting
from the configured view width; the design already allows zoom within the fixed view. Zoom stays
where it is left, R resets it along with the view direction, and each trial records the width it
was answered at.

All three projections show the same width at the tester's ship, so on-screen ship size there is
comparable between them.

The default backdrop is a seeded star field on near-black, drawn at infinity so that it turns
with the camera and ignores zoom. It is darker than the style guide's background baseline, which
the `dark` backdrop still shows as a flat colour.

## What it measures

Space starts a run of three kinds of trial, shuffled:

- **Select:** a hostile is marked briefly; click it. Records time, wrong ship, and clicks on nothing.
- **Heading:** the scene freezes and one ship is marked; click where it is heading. Correct within
  22.5°.
- **Nearer:** the scene freezes and two hostiles are marked; choose the one nearer your ship.

It also records, without the tester, how long any ship's centre stays behind a structure. Results
appear in the panel and download as JSON, including the configuration and every trial.

## Limits

- The limits shown beside results are the review's starting hypotheses. The design leaves gate
  thresholds and cohort sizes open.
- Ships are extruded outlines at the starter frigate's dimensions, and the station is a dozen
  boxes and cylinders taken from `create_scene.py`. Binary assets are Git LFS pointers in a normal
  checkout, so no GLB is loaded.
- The station's landing deck is assumed to lie on the gameplay plane. Neither design document
  defines that datum yet.
- Steering, weapons and NPC routes are placeholders written for this harness. The real rules
  belong in `frontier-sim`.
- Colour-vision modes are approximate simulations, not a substitute for testers.
- Mouse and keyboard only.
