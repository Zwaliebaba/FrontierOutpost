# ADR-075 — A canvas presented at an integer scale

**Status:** Accepted 2026-09-13 (owner, by instructing RENDER-01 stages 1 to 6 to be built)

**Date:** 2026-09-13
**Decided by:** Owner decision, 2026-09-13, in a design session on how the client reaches a second platform without the Windows build ceasing to be one executable.
**Supersedes:** The 1:1 presentation clause of ADR-011, and nothing else in it. This ADR re-admits ADR-011's option C, which ADR-011 rejected "until the presentation stops being 1:1".

---

## Context

ADR-011 fixed the screen at 1280×720 `R8G8B8A8_UNORM`, and then did something more particular than
choosing a resolution: it made the render target, the swap chain's back buffer and the window's
client area the *same* 1280×720 pixels. There is no scale anywhere on that path — ADR-011 says so
in as many words, "the present scale is gone, not set to one", and AGENTS.md R12 repeats it as a
rule. Its own closing open question is the one this document answers: *whether the game should ever
present at a size other than 1280×720 — a resizable window, a fullscreen mode, a second aspect
ratio.*

**What is true today, and was not obviously true then: the screen's numbers are already logical
points, and nobody aimed for that.** Three figures, each taken from a document in this tree rather
than estimated here:

- A rail row is **44 pixels** (ADR-052). 44 points is the iOS touch-target minimum. ADR-052 reached
  it from a thumb on a 1280×720 screen, not from anybody's platform guidance.
- The face is baked at **12px with a 17px line box** (ADR-074; `Design/UI/DESIGN-GUIDELINES.md`
  §Font, measured from the TTFs on 2026-09-13). At 3× that is a 51-pixel physical line, which is
  where body text lands on a 3× phone.
- An iPhone 15's panel is **1179 physical pixels** across (`Design/Reference/mobile-portability.md`
  §5). Divided by three: 393 — its width in points.

None of that is a coincidence in the interesting sense. A screen laid out by eye at a density a
person can read lands near the density a phone's design language was drawn for, because both are
answering the same question about the same human. **What the design lacks is not a better
resolution. It is a scale factor.**

**On the desktop it lacks one now, not later.** The process is per-monitor DPI aware V2
(`Lockstep.cpp`, `SetProcessDpiAwarenessContext`), which is correct and which means Windows hands
the window physical pixels and does not stretch it. On a 27″ 3840×2160 panel at 150% the client
area is therefore 1280×720 *physical* pixels: a third of the screen's width, a ninth of its area,
carrying a 12px line of text that is 1.9 mm of glass. That last figure is arithmetic from the
panel's size and pixel count — 0.156 mm a pixel — and no monitor was measured for it. The window
is not small because the design is dense; it is small because the presentation has no opinion about
the display it lands on.

1280×720 is the integer root of both panels that matter here: ×2 is 2560×1440, ×3 is 3840×2160. The
canvas is the right size. The presentation is the wrong kind of number.

The constraint that binds hardest is `Design/README.md` §1's sentence that **nothing on the path
from a vertex to the display resamples anything**, which this ADR keeps and which is what rules out
the obvious answer below. R12 (Direct3D 12, and no sampler without a decision), R13 (the executable
ships alone) and R14 (the Windows SDK and nothing else) are untouched by any option here.

## Options considered

### A. Keep 1:1, and let the window be the size it is

Costs nothing and changes nothing. It is a defensible position for a prototype whose author runs it
on one monitor.

Rejected because the monitor it is bad on is the one the game is developed and shown on, and
because the second platform `Design/blueprint.md` §7 names cannot be reached from a presentation
that has no scale at all — a phone hands you a surface in physical pixels and asks what you intend
to do with it, and "present 1:1" is not an answer. Deferring this does not make it smaller; it
makes it a change to a larger tree.

### B. A resizable window with an aspect lock and a free scale

Let the user drag the window; keep 16∶9; present the canvas at whatever fractional scale the client
area implies. This is what most applications do and it gives the smoothest result on paper.

Rejected on the baseline. A non-integer scale resamples, and `Design/README.md` §1 forbids
resampling on this path; ADR-011 kept `Load()` over a sampler for exactly this reason and R12 makes
adding a sampler a decision in its own right. The second objection is about the type rather than the
rule: a **12px hinted bitmap** face is not a face that survives 1.5×. ADR-074 records that hinting
at 12px is what produces any fully covered pixel at all — unhinted there are *zero* — so a
fractional scale would take the one thing that was tuned to the pixel grid and smear it across two.
The result would not be a slightly softer version of this screen. It would be a different screen.

### C. A 1280×720 canvas, presented at the largest whole scale that fits

The game draws into its own `R8G8B8A8_UNORM` 1280×720 surface. One fullscreen pass copies that
surface into the back buffer at an integer scale, centred, with the remainder cleared to black. The
window (later: the surface) need not be a multiple of the canvas, because the letterbox absorbs
whatever is left over. The copy reads the canvas with a texel `Load` and an integer divide of
`SV_Position`, so there is still no sampler and still nothing filtered.

This is ADR-011's option C, described there as "exactly the resolve pass this ADR is deleting, minus
the palette", and rejected then as "machinery ahead of its case". Its case has arrived: a 4K desktop
today, and a phone surface later, are the same problem with different numbers.

## Decision

**The game draws a fixed 1280×720 `R8G8B8A8_UNORM` canvas and presents it at the largest whole-number
scale the display has room for, letterboxed.** Option C. The scale is an integer at least 1, chosen
once at startup; nothing on the path from a vertex to the display resamples anything, because the
resolve pass reads the canvas with an integer texel load and an integer divide of the pixel
position.

The canvas, not the back buffer, is what every existing pass draws into, and every number in
`Design/UI/DESIGN-GUIDELINES.md` remains a canvas pixel. The back buffer is now only a place the
finished canvas is copied to. The letterbox is black.

The back buffer stays `R8G8B8A8_UNORM` and deliberately not `_SRGB`, unchanged from ADR-011 and for
its reason: a channel authored as `0xAA` is presented as `0xAA`, so a capture can be compared
against the source byte for byte. **A capture taken at scale 1 is still exactly the picture ADR-011
described**, which is what lets every stage of the change that implements this be checked against a
byte-identical screenshot.

## Consequences

**What this costs, plainly.** One 1280×720 `R8G8B8A8` surface — 3.5 MB — and one fullscreen pass a
frame, both of which ADR-011 deleted and this ADR puts back minus the palette lookup. Neither was
measured before or after and neither is claimed as a performance result in either direction; at this
scale they were negligible when they were removed and they are negligible now.

**On a 1080p panel, nothing changes at all.** 1280×720 doubled is 2560×1440, which does not fit on a
1920×1080 display, so the scale is 1 and the window is the window it is today.

**On a 1440p panel at 100%, nothing changes either, and that is worth saying out loud.** A 2560×1440
client area does not fit inside a 2560×1440 monitor once a caption bar and a taskbar have taken
their rows, so the largest scale that fits the *work area* is still 1. Reaching scale 2 on that
panel needs a window with no caption covering the whole monitor — borderless fullscreen — and that
is a separate owner decision, deliberately not taken here.

**The staircase scales with the canvas.** A diagonal lane on the map that steps one pixel today
steps three pixels at 3×, because the geometry is rasterized into the canvas and the canvas is
magnified. It is the cheapest path, it is the only one under which a scale-2 picture is a
derivable function of the scale-1 capture every stage of RENDER-01 is checked against, and it is
consistent with what ADR-014 chose when it refused a signed-distance shader: an edge on this screen
is where a designer's arithmetic put it, not where a rasterizer decided. The alternative — draw the map's geometry at the display's native resolution
and magnify only the glyphs and the interface — is a real option and it is **not** taken here. It
should be decided from a screenshot on a phone, which is where the difference is large enough to
argue about.

**The scale is decided once.** A window dragged from a 4K monitor to a 1080p one keeps its physical
size and hangs off the edge of the smaller display, which is what per-monitor V2 awareness asks for
and is not what a user expects. Recorded as an open question rather than solved, because solving it
means resizing a swap chain and this decision does not need one.

**The canvas's alpha channel is inert, and this is worth being exact about.** The one blend state in
this renderer, `D3D12Defaults.h`'s `StraightAlphaBlend`, sets `SrcBlendAlpha = ONE` and
`DestBlendAlpha = ZERO`: the colour channels composite against the destination, the alpha channel is
overwritten by whatever the shader returned, and destination alpha is never read by anything. The
scene passes do not blend at all. So the canvas's alpha is whatever the last shader to touch a pixel
put there, nothing downstream consults it, and the resolve pass writes 1 into the back buffer
regardless. This is what makes a glyph shader free to return a zero alpha instead of discarding,
which is a thing a tile-based mobile GPU cares about and this one does not; a reader who assumes the
canvas's alpha is meaningful will conclude the opposite.

**What this makes easy.** A phone. `Design/Reference/mobile-portability.md` §5's arithmetic was never
redone for 1280×720, and this decision is the shape that answers it: the same integer-scale,
letterboxed contract, applied to a surface an operating system hands over rather than to one asked
for from a window manager. The struct that centres a canvas inside a surface and maps a pointer back
onto it is platform-free and is reused unchanged. It also makes borderless fullscreen a small
change rather than a design question, and it re-opens the door to anything that wants to read the
finished frame — a fade, a transition, a post-process — none of which exist and none of which are
promised here.

**What it forecloses.** Nothing that was wanted, and less than it looks. 1:1 is `--scale 1`.

## What this changes elsewhere

- **AGENTS.md:** the preamble sentence and R12's first paragraph both state the 1:1 presentation as
  a rule and are rewritten against this ADR; §2's `NeuronClient/` row gains "presented at an integer
  scale". R12's sampler paragraph, its D3D11 ban and its COM-lifetime rule are unchanged — the
  resolve pass is a `Load`, so the no-sampler rule survives this ADR intact.
- **Design/:** `README.md` §1's Presentation row, `UI/DESIGN-GUIDELINES.md`'s Frame line and
  `UI/README.md`'s Non-negotiables are rewritten. `Reference/mobile-portability.md` §10's second
  decision is answered. ADR-011's status line records that its 1:1 clause is superseded; nothing
  else in ADR-011 is edited, because an Accepted ADR is immutable but for that line.
- **Code:** nothing yet. `Design/Plans/RENDER-01-CanvasAndRecorder.md` is the plan, and no stage of
  it starts until this ADR is Accepted. `NeuronClient/SceneTarget` grows the canvas and the resolve
  pass; `NeuronClient/Presentation.h` is new and platform-free; two shaders are added;
  `Lockstep.cpp` chooses the scale and sizes the window; `NeuronClient/PointerInput` maps a pointer
  from the surface onto the canvas.
- **Captures:** `Design/UI/screens` stays 1280×720 PNGs of the canvas, taken at `--scale 1`. They do
  not need retaking for this decision, and if one of them changes, something is wrong.

## Open questions

**Whether the window follows `WM_DPICHANGED`.** This ADR says no: the scale is chosen once at
startup and a window dragged between monitors of different DPI keeps its physical size. Handling it
properly means recomputing the scale, resizing the swap chain and rebuilding the presentation
mid-frame, which is the same machinery borderless fullscreen needs and is best decided with it.

**Whether borderless fullscreen ships.** It is the only way a 1440p panel at 100% ever sees scale 2,
and it is the only thing here that resizes a swap chain. An owner decision, and everything else in
this ADR works without it.

**Whether the map's geometry ever draws at native resolution.** See the staircase, above. Left for a
screenshot on a real phone to settle, because on a desktop monitor at 2× the question is too small
to answer honestly.

**What the mobile canvas is.** Not this one. `Design/blueprint.md` §9 still owes an ADR revising the
baseline for a portrait canvas, and this decision deliberately does not pre-empt it: it fixes the
*contract* — an integer scale and a letterbox, with no resampling — and leaves the portrait
canvas's size and layout to the document that has a phone in front of it.
