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
asteroid, a fight around the home station (the real `station.glb`, as gate 2 asks), and ships
heading toward and away from the camera. The tester steers one ship with W A S D or the arrow keys.

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
- Player-side ships are the starter frigate (`frigate.glb`) and the station is `station.glb`, its
  sockets empty, both as the game draws them. If either fails to load, the harness falls back to
  an extruded wedge, or to a dozen boxes and cylinders taken from `create_scene.py`, and says so in
  the console. Downloaded results record which it measured (`hull`, `station`); results from
  before these fields measured the wedge and the proxy. Hostiles are an extruded forked outline in
  the harness and the game alike: there is no hostile model yet.
- The station stands where the game stands it: its deck half a hull below the gameplay plane,
  where a ship's underside rests (`STATION_DROP` in `src/outpost/placement.ts`). Whether a ship is
  hidden is tested against the model's own triangles.
- Steering, weapons and NPC routes are placeholders written for this harness. The real rules
  belong in `frontier-sim`.
- Colour-vision modes are approximate simulations, not a substitute for testers.
- Mouse and keyboard only.
