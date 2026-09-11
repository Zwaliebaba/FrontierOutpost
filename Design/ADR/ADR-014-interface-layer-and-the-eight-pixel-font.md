# ADR-014 — The interface is two renderers and an immediate-mode screen, and the 8×8 font sets the copy

**Status:** Accepted — amended by ADR-034 (2026-09-11): the interface layer gains one minimal text field, for the join screen's server and token. Everything else here stands.

**Date:** 2026-09-10
**Decided by:** Build session implementing `Design/Screens` (the main page). The handoff settled colour, layout and copy; this ADR records what it did not settle, and the places where the game's one font could not carry its copy.
**Supersedes:** —

---

## Context

`Design/Screens/README.md` specifies the single screen of Frontier Outpost at high fidelity: a
1280×720 ops console of three columns — digest rail, map, orders rail — with final colours,
spacing and copy, and a projection for the map that is the spec rather than an illustration.

The tree it had to be built into had **no interface layer at all**. `NeuronClient` could draw an
authored mesh through a perspective camera, a procedural starfield, and 8×8 text; it could not
draw a rectangle. There was no widget type, no layout, no hit testing, and no way to put a line
on the screen. Everything below follows from building that in the shape the rest of the tree is
written in, rather than importing one.

Two constraints bind hard and are worth stating before the options. **The game has one font**:
96 glyphs of ASCII, 8×8, one bit a pixel, embedded in the binary (R13, `Font.h`). And ADR-011
left the renderer with **no blending anywhere** and R12 requiring an ADR for any pass that wants
it — which this is.

## Options considered

### A. A retained widget tree

The conventional answer: types for a panel, a label, a button; a layout pass; a paint pass; event
routing down the tree. It is what every UI toolkit is, and it pays for itself when a screen has
many states and long-lived elements a user manipulates directly.

It is rejected because it is the wrong shape for **this** screen. The digest is replaced wholesale
every tick — seven events in, seven different events out — so the retained nodes would be rebuilt
every time the only interesting thing happened. A tree also introduces the failure this codebase
has been careful to keep out elsewhere: a node that disagrees with the state behind it. The
client's whole discipline is that it holds no rules and renders what the server told it
(ADR-005); a widget with its own copy of a digest event is a second source of truth.

### B. Immediate mode: draw from state every frame, record the tappable rectangles as you go

The screen is a function of `MatchState`. Layout is recomputed each frame, and each control
appends its rectangle to a list as it is drawn; a tap is tested against that list. Layout and hit
testing are then the *same code read twice*, and cannot drift apart — the class of bug where a
button moves and its hit box does not simply has nowhere to live.

The cost is that layout runs every frame for a screen that changes once a tick, and that a tap is
tested against the previous frame's rectangles. The first is a few hundred microseconds of
arithmetic. The second is one frame of staleness, which at any frame rate a person can tap through
is not observable — and the screen has no drag, so there is no gesture that could notice.

It also has a real limit worth writing down: there is no scrolling, no focus order and no keyboard
navigation, because immediate mode gives you none of those for free. The main page needs none of
them. The first screen that does is the one that reopens this question.

### C. Two renderers, or one

Text already had a renderer. The choice was whether shapes join it or stay separate.

They are separate — `ShapeRenderer` and `FontRenderer`, one draw call each — because they differ
in the thing that matters to a pipeline: the text pass binds the font atlas and discards
uncovered pixels; the shape pass binds no texture at all. Merging them would mean a shader that
branches on whether a vertex is textured, on a screen where the answer is known at authoring
time.

## Decision

**The interface is `NeuronClient::ShapeRenderer` plus `NeuronClient::FontRenderer`, and the screen
is `Frontier::MainPage`, drawn immediate-mode from `Frontier::MatchState`.** Option B.

`ShapeRenderer` tessellates on the CPU into one triangle list: a rectangle is two triangles, a
line is a quad, an ellipse is a fan, a dash pattern is several of the above. There is no
signed-distance shader, and that is deliberate — an SDF gives smooth edges, and a smooth edge is
coverage the rasterizer did not decide, which on a screen built from named colours puts a colour
on a pixel that nobody chose.

**Blending is enabled for the two interface passes and for nothing else.** This is the exception
R12 asks to be recorded. The scene passes stay opaque: every surface in the world is one of two
authored tones and has nothing to blend (ADR-012). The interface is different in kind — six of its
design tokens are translucent greys over known backgrounds, and the alternative, compositing each
against its backdrop on the CPU, stops working at the first lane that crosses the map's gradient,
which is the first thing the map does. `D3D12Defaults.h` gains `InterfaceBlendState()`; the
default state is unchanged, so a new pass is still opaque unless it says otherwise.

**Alpha in these passes is a material, not coverage.** A card fill really is 4% white; a glyph
pixel is still lit or discarded with nothing in between.

**The glyph scale becomes a per-call argument**, and `FontRenderer::GLYPH_SCALE` is gone. ADR-013
made it a single constant and said the UI design was where it should be settled; the UI design has
now settled it as *two* values — 1× (8px) everywhere, 2× (16px) for the lock countdown in the top
bar and the orders footer, and nothing else. One constant could not say that.

**Map labels do not scale with depth.** `Design/Screens/README.md` says node size, label size and
stem height all scale with `s`; the reference file draws every map label at one size
(`font-size="9.85"`, which after the 0.8125 letterbox is exactly 8px on screen). The reference is
followed: geometry takes the depth scale, text does not. With one font at whole-number scales
there was no third option, and the reference shows the intended result.

**The executable shows the main page.** The MVP-01 isometric ship scene is no longer what
`FrontierOutpost.exe` renders. Its code — `MeshRenderer`, `IsometricCamera`, `Starfield`,
`ShipMesh`, `StationMesh`, `ShipView`, `World`, `Session` — is retained, still built and still
covered by the test suites, but is not reachable from `RunGame`. A mode switch between the two was
not asked for and is not invented here.

### What the 8×8 font could not carry

Five characters in the reference copy are not among the font's 96 glyphs. They are substituted
once, in `MatchFixture.cpp`, so the strings in that file are the strings on the screen:

| Reference | Used | Where |
|---|---|---|
| `·` middot separator | `-` | everywhere |
| `−` true minus | `-` | build costs |
| `–` en dash | `-` | `Orune–Kepler-Reach` |
| `→` right arrow | `>` | `HALVORSEN → you`, `→ Kepler-Reach` |
| `›` angle quote | `>` | `change ›` |

The `▶` replay glyph is **not** substituted: it is geometry in the reference too (a CSS border
triangle), and it is drawn as a triangle here.

Four strings do not fit at 8px and are shortened. Each shortening uses a form the screen already
uses elsewhere, so none of them introduces a new convention:

| Reference | Shortened | Why |
|---|---|---|
| `TICK 47 LOCKS IN` | `T47 LOCKS IN` | the top bar is 63px over the frame at 8px; T-notation is already the screen's form (`ETA T47`, `CAPTURED T45`) |
| `REPLAY TICK 46` | `REPLAY T46` | same bar, same reason |
| `LEADER HALVORSEN 1,610` | `LEAD HALVORSEN 1,610` | same bar |
| `DAY 12 / 21` | `DAY 12/21` | same bar |
| `Trade lane with HALVORSEN` | `Trade lane - HALVORSEN` | 200px against 196px of column beside the PROPOSE button |
| `ALL THREE LOCK TOGETHER` | `ALL 3 LOCK TOGETHER` | 312px against 301px beside a 2× countdown; the screen writes digits everywhere else |

**Everything else wraps rather than truncating.** The digest rail holds 31 characters a line and
most of the reference's digest copy is longer, so titles and details wrap to two lines — which is
what the reference file itself does. A digest that hid its second half would not be a digest.

## Consequences

**What this makes easy.** A new panel is a function that draws rectangles and text and appends hit
rectangles; there is no type to register, no layout to configure and no event plumbing. Adding a
colour is a line in `Color.h`. The screen is also verifiable by screenshot in a way a retained
tree would not be, because a frame is a pure function of `MatchState`.

**What this makes hard.** No scrolling, no keyboard focus, no text input — the last of which the
game does not want in any case (the one-pager has no free text in v1). A digest longer than the
rail would run off the bottom rather than scroll; at seven events it does not, and the fix when it
does is a scroll offset in `MainPage`, not a widget system.

**What it costs.** Layout every frame, and a one-frame-stale hit list. Neither was measured
because neither is plausibly measurable at this scale: the whole screen is about 1,600 characters
and under 12,000 vertices.

**What it forecloses.** Nothing structurally — option A remains available for a screen that needs
it, and would sit beside this rather than replacing it.

**A real gap.** `MainPage`'s own logic — the projection, the lane-cost Dijkstra behind the
destination picker, the countdown format — is **not unit-tested**, because it lives in
`FrontierOutpost.exe` and the executable has no test suite (AGENTS.md §2 gives one to each of the
four libraries). Text wrapping was moved to `FontRenderer` partly for this reason and is tested
there. Closing the rest means either a fifth suite for the executable or moving `MapProjection`
into `NeuronClient`; neither was done here, and the second is probably right.

## Verification

Measured on 2026-09-10 from the running executable at 1280×720:

- The screen renders and matches the reference's layout, colour and content. The map letterboxes
  to 650×455 in a 650×672 pane at scale 0.8125, which places the horizon at y=205 and the near
  edge at y=562.
- The projection is the reference's: every one of the twelve node positions in the reference SVG,
  run back through the inverse of `MapProjection`, lands on a round design-space coordinate to
  within a hundredth (`Vesk` → (180, 300), `HALVORSEN` → (470, 180), and so on).
- The interactions work: a digest tap focuses its system on the map, a build row queues, the
  proposal accepts, and tapping `FLT 1` opens a picker offering exactly Vesk's three lane
  neighbours with their tick ETAs.
- All three checkers pass, and the four suites pass 108 tests.

## What this changes elsewhere

- **Code:** `NeuronClient/ShapeRenderer.{h,cpp}`, `NeuronClient/Shaders/Shape{VS,PS}.hlsl` are new.
  `FontRenderer` gains a per-call scale, measurement and wrapping, and its per-frame character
  budget rises from 512 to 4,096. `D3D12Defaults.h` gains `InterfaceBlendState()`.
  `FrontierOutpost/{MainPage,MatchState,MatchFixture,MapProjection}.*` are new;
  `FrontierOutpost.cpp` runs the main page.
- **AGENTS.md:** no change. R12 already required this ADR for the blending exception.
- **Design/:** ADR-013's `GLYPH_SCALE` is revised — the scale is per-call, and its status line says
  so. Nothing superseded.

## Open questions

Whether `MapProjection` belongs in `NeuronClient` rather than in the executable. It is arithmetic
with no game vocabulary in it, and moving it would make it testable, which is the argument.
Against: it is a projection chosen for how this game's map should look, not a general one.

Whether the digest needs scrolling. Seven events fit; the design rule is one digest per tick, and
nothing bounds how many events a tick can produce.

What `Replay tick N` actually shows. It opens a stub listing the six phases of tick resolution and
saying so. Stepping through them needs resolved state the server does not yet send.
