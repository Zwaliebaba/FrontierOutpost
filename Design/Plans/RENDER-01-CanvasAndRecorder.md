# RENDER-01 — A canvas presented at an integer scale, and a renderer that records before it draws

**Status:** Proposed. Written 2026-09-13 from a design session on how the client reaches a second
platform without the Windows build ceasing to be one executable. Nothing in this plan is built.

**What this plan is for.** Two things, in a fixed order. First, the presentation stops being
"1280×720 physical pixels, drawn straight into the back buffer" and becomes "a 1280×720 canvas,
presented at a whole-number scale, letterboxed" — which is what a 4K desktop needs today and what
a phone needs later. Second, the two renderers the interface is made of are split into a
platform-free *recorder* the pages and the tests talk to, and a D3D12 *backend* that drains it.
After this plan the only files in the tree that name D3D12 are `NeuronClient/Device`,
`NeuronClient/SceneTarget`, the two backends, `D3D12Defaults.h`, `DescriptorHeap` and the
composition root — and a Vulkan or Metal build is a second backend behind the same recorder,
not a rewrite.

**What this plan is not.** It does not build a second backend, a portrait layout, a variable-size
canvas, a lifecycle model, a portable input decoder, a CMake build or a portable test harness.
Every one of those is on the road to the mobile client `Design/blueprint.md` §7 names, and every
one of them is deliberately out of scope here. It also does not cut `GameLogic` or `NeuronServer`
loose from `NeuronCore.h`'s Windows include graph; `Design/Reference/mobile-portability.md` §3
says that is a one-line change, and it stays a separate one.

**Read first:** ADR-011 (what the presentation is today and why option C was rejected *then*),
ADR-014 (the interface is two renderers), ADR-041 (why the renderers have a headless mode),
ADR-074 (coverage as alpha in the text pass), `Design/Reference/mobile-portability.md` §5 and
§10, and AGENTS.md R12, R13 and §3. The session that executes a stage reads the files that stage
names before editing them; the plan describes the shape, the code is the fact.

**The ordering is the point.** Stages 1 through 6 each end at a byte-identical capture of the join
screen at scale 1. That test exists only while the picture is unchanged, and it is the strongest
evidence this plan has, so nothing here changes what a player sees at scale 1. The one visible
change is a bigger window on a monitor that has room for one, and that arrives in stage 2 as a
new *size*, never a new *picture*.

---

## Stage 0 — The decision, recorded before the code moves

**Why this is a stage.** AGENTS.md R12 says, in as many words, "no intermediate render target,
no resolve pass and no present scale", and AGENTS.md's own rule is that a task which cannot be
done without deviating says so rather than deviating silently. So the first commit is the ADR
and the rule edits, and no code stage starts until the owner has marked the ADR Accepted.

**Write `Design/ADR/ADR-075-a-canvas-presented-at-an-integer-scale.md`** from the template, with
this content. Status Proposed; decided by owner decision on 2026-09-13 in a design session on
cross-platform rendering; supersedes the 1:1 presentation clause of ADR-011 and re-admits ADR-011's
option C, which ADR-011 rejected "until the presentation stops being 1:1".

*Context to state.* The screen's numbers are already logical points and nobody aimed for that: a
44-pixel row (ADR-052) is the iOS touch-target minimum in points; a 12px face with a 17px line box
scaled 3× is a 51px physical line, which is iOS body text on a 3× device; an iPhone 15 panel
divided by three is 393 logical pixels, its point width. What the design lacks is not a better
resolution but a scale factor. At 1:1 on a 4K monitor at 150% the window is a third of the screen
carrying 2mm text. 1280×720 is the integer root of 2560×1440 and 3840×2160, so the canvas is the
right size; the presentation is the wrong kind of number.

*Options.* A: keep 1:1 and accept the window as it is. B: a resizable window with an aspect lock
and a free scale — rejected because a non-integer scale resamples, which `Design/README.md` §1
forbids, and because a fixed 12px hinted bitmap face is not a face that survives 1.5×. C: an
offscreen 1280×720 canvas presented at the largest integer scale that fits the monitor's work
area, letterboxed in a window (or surface) that need not be a multiple of the canvas, read back
with a texel `Load` and an integer divide of the pixel position so nothing on the path from a
vertex to the display resamples anything. C is the decision.

*Consequences to state honestly.* One 1280×720 R8G8B8A8 surface (3.5 MB) and one fullscreen pass
per frame — the resolve pass ADR-011 deleted, minus the palette. On a 1080p panel nothing changes.
On a 1440p panel at 100% *nothing changes either*, because a 2560×1440 window does not fit under
a caption and a taskbar; reaching scale 2 there needs borderless fullscreen (stage 7, an owner
decision). The staircase on a diagonal lane scales with the canvas: at 3× it is a three-pixel
step, which is consistent with ADR-014's pixel-grid language and is the cheapest and most
testable path; native-resolution geometry with integer-scaled glyphs is the alternative, and it
is left for a phone screenshot to decide. The scale is decided once at startup; a window dragged
to a monitor of a different DPI keeps its physical size (open question, below). The canvas's
alpha channel is inert: nothing blends against destination alpha, and the resolve writes 1.

*What this changes elsewhere* — make these edits **in the same commit as the ADR**:

- `AGENTS.md` line 5 (the preamble): "presenting a fixed **1280×720 R8G8B8A8** screen, drawn
  straight into the swap chain's back buffer and presented 1:1" becomes "drawing a fixed
  **1280×720 R8G8B8A8** canvas and presenting it at a whole-number scale (ADR-075)".
- `AGENTS.md` R12: replace "drawn straight into the swap chain's back buffer, whose client area is
  those same 1280×720 pixels. There is no intermediate render target, no resolve pass and no
  present scale (ADR-011)" with "drawn into a 1280×720 canvas and presented into the back buffer
  at a whole-number scale by one resolve pass that reads the canvas with a texel `Load` and an
  integer divide — there is no sampler on that path either, and the scale is never fractional
  (ADR-075, superseding ADR-011's 1:1 clause)". Leave the rest of R12 — no D3D11, COM is RAII,
  the sampler paragraph — exactly as it is.
- `AGENTS.md` §2 table, the `NeuronClient/` row: "the 1280×720 colour target" stays true; add
  "presented at an integer scale".
- `Design/README.md` §1, the Presentation row: rewrite to "**1280×720, R8G8B8A8**, a canvas
  presented at the largest **whole-number scale** the display has room for, letterboxed. Nothing on
  the path from a vertex to the display resamples anything: the canvas is read with an integer
  texel load (ADR-075). Until 2026-09-13 the canvas was the back buffer, presented 1:1 (ADR-011);
  until 2026-09-10 it was 640×400 in sixteen colours, blown up 2× (ADR-001)."
- `Design/UI/DESIGN-GUIDELINES.md` line 9: "1280×720 logical pixels, fixed and presented 1:1
  (ADR-011). No scaling factor, no fullscreen." becomes "1280×720 logical pixels, fixed, presented
  at a whole-number scale chosen at startup (ADR-075). Every number in this document is a canvas
  pixel."
- `Design/UI/README.md` line 214 and the `screens/` note: captures are taken at `--scale 1`
  (stage 2) and remain 1280×720 PNGs of the canvas.
- `Design/Reference/mobile-portability.md` §10, second decision: append "*Decided 2026-09-13,
  ADR-075: 1280×720 is the desktop canvas, presented at an integer scale. The mobile canvas is
  `floor(surface / round(density))` logical pixels in portrait, and its layout is the new client's.*"
- ADR-011's status line: "Accepted; the 1:1 presentation clause superseded by ADR-075."

*Open questions to leave in the ADR.* Whether the window follows `WM_DPICHANGED` (this plan: no,
scale is chosen once). Whether borderless fullscreen ships (stage 7). Whether the map's geometry
ever draws at native resolution.

**Verification.** `py Build\CheckFormat.py` and `py Build\CheckProjectFiles.py` pass (they read
the Markdown for nothing, but the commit must be green). The owner reads the ADR and changes its
status to Accepted. **Stop here until that has happened.**

**The gate opened 2026-09-13.** The owner did not edit the status line; they instructed stages
1 to 6 to be built, which is the same decision and is recorded in ADR-075's status line in those
words. Stage 7 was not included and stays unstarted.

**Stage 0, as run (2026-09-13).** ADR-075 is written and every edit it lists is made, in one commit.
Both checkers are green. `RunClangTidy.py` was not run and is not claimed: nothing in this commit is
C++, and the stage's gate above is the two checkers for that reason.

**What stage 0 found, which is not about this plan at all.** `CheckProjectFiles.py` failed on the
first run, with the line-endings message the merge at 671ef8d added the same day: `Build/BakeFont.py`
was still CRLF in this working tree, checked out before `.gitattributes` pinned `*.py` to `eol=lf`,
so it hashed to something `NeuronClient/Font.h` has never recorded. **The fix is a re-checkout of the
file, not a re-bake** — which is exactly what the new message says, and it earned its place: the
obvious response to "Font.h disagrees with its source" is to re-run the baker, and doing that here
would have recorded a CRLF hash no CI runner can reproduce. `Build/CheckFormat.py` was stale the same
way and was renormalised with it. **Anyone pulling 671ef8d onto an existing Windows checkout hits
this**, because `.gitattributes` changes what git *would* write and touches nothing already on disk.

**A staleness found beside the edit, and corrected on the owner's instruction.**
`Design/UI/README.md`'s Non-negotiables still said "One 8×8 bitmap font at 1× … No anti-aliasing",
which ADR-074 overtook on 2026-09-13. It was reported rather than fixed with the rest of stage 0,
because widening a change to a neighbouring sentence is the thing stage 6 is told not to do; the
owner asked for it directly, so the paragraph now describes the four Plex cuts and says that a
glyph's coverage is the one thing on the screen a rasterizer decides. The passage at the top of the
same file that describes the 8×8 font in the past tense is correct and was left alone.

**One inconsistency is knowingly accepted, and it closes at stage 1.** Between this commit and stage
1, AGENTS.md and `Design/README.md` §1 describe a canvas the code does not draw yet, which is in
tension with AGENTS.md's own preamble — "it describes the code as it must be written today".
`Design/README.md` §4 asks for exactly this ordering (the ADR, and everything it invalidated, in one
commit), and the alternative is a stage 1 that deviates from R12 silently. The window is one stage
long and the ADR is Proposed for all of it.

**Commit:** `Decide that the screen is a canvas presented at an integer scale`

---

## Stage 1 — The canvas, presented at scale 1

**What changes.** `SceneTarget` grows back into what it was before ADR-011 minus the palette: it
owns the colour canvas as well as the depth buffer, and it owns the pass that copies the canvas
into the back buffer.

`NeuronClient/SceneTarget.h` / `.cpp`:

- `Create(Device& _device, DescriptorHeap& _shaderVisibleHeap, const Color& _clearColor)`. It
  creates a committed `R8G8B8A8_UNORM` 1280×720 texture with `ALLOW_RENDER_TARGET`, initial state
  `RENDER_TARGET`, optimised clear value = `_clearColor`; its own one-slot RTV heap; an SRV for it
  in the shader-visible heap (one `Allocate()`); the depth buffer as today; and the resolve
  pipeline (below).
- `BeginScene(ID3D12GraphicsCommandList*)` no longer takes a back-buffer view. It binds the canvas
  RTV and the DSV, sets the 1280×720 viewport and scissor, clears both. Callers stop passing
  `device.BackBufferView()`.
- New `Present(ID3D12GraphicsCommandList* _commandList, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView, const Presentation& _presentation)`:
  barrier canvas `RENDER_TARGET → PIXEL_SHADER_RESOURCE`; bind the back buffer with no depth;
  viewport = the whole back buffer; clear the back buffer to `BLACK` (the letterbox colour);
  scissor = the canvas rectangle `(offsetX, offsetY, 1280·scale, 720·scale)`; set the resolve
  root signature, pipeline, the shader-visible heap, the SRV table and three root constants
  (`offsetX`, `offsetY`, `scale`, all `uint`); `DrawInstanced(3, 1, 0, 0)`; barrier back to
  `RENDER_TARGET`. The scissor is what keeps the shader's subtraction from going negative and the
  letterbox cleared.
- `Presentation` is a new **platform-free** aggregate in `NeuronClient/Presentation.h` (no Win32,
  no D3D12 in it; R8 says plain fields):
  `std::uint32_t scale; std::uint32_t offsetXPixels; std::uint32_t offsetYPixels; std::uint32_t surfaceWidthPixels; std::uint32_t surfaceHeightPixels;`
  with a `static constexpr Presentation For(surfaceWidth, surfaceHeight, scale)` that centres the
  canvas, and a `[[nodiscard]] bool ToCanvas(float _surfaceX, float _surfaceY, float& _outX, float& _outY) const noexcept`
  that divides and offsets and returns false outside `[0,1280)×[0,720)`. Stage 3 uses `ToCanvas`;
  stage 1 only needs `For`. This struct is the seam a phone reuses unchanged.

Two new shaders, `NeuronClient/Shaders/CanvasVS.hlsl` (a fullscreen triangle from `SV_VertexID`,
no vertex buffer — the shape ADR-001's resolve pass had) and `CanvasPS.hlsl`:

```hlsl
Texture2D<float4> g_canvas : register(t0);
cbuffer CanvasConstants : register(b0) { uint2 g_offsetPixels; uint g_scale; };
float4 main(float4 _position : SV_Position) : SV_Target
{
  int2 texel = (int2(_position.xy) - int2(g_offsetPixels)) / int(g_scale);
  return float4(g_canvas.Load(int3(texel, 0)).rgb, 1.0);
}
```

Register both in `NeuronClient.vcxproj` as `FXCompile` items with their `ShaderType`, and in
`NeuronClient.vcxproj.filters`; `Build/CheckProjectFiles.py` fails otherwise, by design. The
pipeline uses `D3D12Defaults.h`'s opaque blend state, no depth, `RTVFormats[0] =
R8G8B8A8_UNORM`, and the sampler rule holds: it is a `Load`.

`Lockstep/Lockstep.cpp`: the three frame loops (`RunSeatsScreen`, `RunJoinScreen`, `RunGame`)
call `screen.BeginScene(commandList)` and, after the last flush and before
`device.EndFrameAndPresent()`, `screen.Present(commandList, device.BackBufferView(), presentation)`.
In this stage `presentation = Presentation::For(1280, 720, 1)` — the window is still exactly the
canvas. `screen.Create` now takes `device` and `shaderVisibleHeap`, so it moves below the heap.

`Tests/NeuronClientTests`: add `PresentationTests` with `For(1280,720,1)` giving zero offset and
`For(2560,1440,2)` giving zero offset, `For(1920,1080,1)` giving `(320,180)`.

**Verification.** Three checkers green. Debug|x64 and Release|x64 build. Five suites pass.
**Before touching code**, capture the join screen from the pre-stage build with
`Build\Screenshot.ps1` and keep its SHA-256; after the stage, capture again and compare — same
length, same hash. Run the Debug build and confirm `Device::DrainDebugMessages` reports nothing
(a missing barrier shows up here, not on screen).

**Stage 1, as run (2026-09-13).** Three checkers green, Debug and Release both build, 527 tests
pass (ten of them new), and the join screen at scale 1 is **byte-identical**: SHA-256
`a9d82e21…2880f1bf`, 19,439 bytes, the same hash the pre-stage build produced. The D3D12 debug
layer says nothing.

**The byte-identical test has a second outcome, and the plan leans on that test for five more
stages.** The join screen's caret blinks on a **wall-clock** period of one second, lit for 0.6 of it
(`JoinPage.cpp`, `BLINK_SECONDS`), and `Screenshot.ps1` fires a fixed time after the window appears
— so which phase it catches depends on how long the process took to get there. The caret-off
capture differs from the caret-on one in **exactly seven pixels**, `(492..498, 349)`, which is the
7px Plex Mono advance of `_` in `BLUE` over the field fill. Both were seen on the same binary
within a minute of each other:

| phase | SHA-256 | bytes |
|---|---|---|
| caret on | `a9d82e21…2880f1bf` | 19,439 |
| caret off | `9641ae45…90fb1970` | 19,425 |

At a two-second settle the lit phase is what comes back — six runs out of six — which is why
FONT-01 and this stage both read as clean. **A stage that gets the other hash has not broken the
picture**, and a session that assumes otherwise will go looking for a renderer bug that is a
blinking underscore. The check for the rest of this plan is therefore: capture until the caret-on
hash appears, and if a different hash turns up, diff the two PNGs before concluding anything — a
real regression is not seven pixels on row 349.

**The debug-layer check was proved rather than assumed.** "DrainDebugMessages reports nothing" is
worth nothing if the info queue is null, which it is on a machine without the Graphics Tools
feature (`Device.h` says so). It is present on this one: with the canvas's
`RENDER_TARGET → PIXEL_SHADER_RESOURCE` barrier temporarily deleted, the layer reported
`RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH #527` and the break fired. The barrier was put back. Reading
`OutputDebugString` without a debugger needs a DBWIN listener — `DebugTrace` is
`OutputDebugStringA` — and that is a scratch script, not a thing in the tree.

**Two deviations from what this stage was written to do, both smaller than the plan's version.**
`SceneTarget::Create` takes `ID3D12Device*` and not `Device&`: it creates two resources and a
pipeline and never needs a queue, so `Device&` would have widened a dependency for symmetry with
`FontRenderer` alone, and the call site already passed `device.Handle()`. And the canvas size now
lives in `Presentation.h` as `CANVAS_WIDTH_PIXELS` / `CANVAS_HEIGHT_PIXELS`, with
`SceneTarget::WIDTH_PIXELS` defined from it: `Presentation` is the header a second platform reuses
unchanged, so it cannot take the number from a class that owns D3D12 resources, and the eleven
existing call sites keep the spelling they have.

**The gates caught two things in code written this session, which is what they are for.**
`CheckProjectFiles` rejected a test method named `…Centres…` under R11, and `clang-tidy`
rejected eight `constexpr Neuron::Presentation presentation` locals under R3 — a `constexpr` is
`UPPER_CASE`. The second is worth the note because the fix improved the tests rather than
placating a rule: they now read `EXACT`, `DOUBLED`, `LETTERBOXED` and `TOO_SMALL`, which is what
each case is.

**Commit:** `Draw into a canvas and present it through a resolve pass`

---

## Stage 2 — A window sized to the largest scale that fits

**What changes.** `Lockstep/Lockstep.cpp` only.

- `ChooseScale(HINSTANCE)`: take the primary monitor's work area
  (`MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY)`, `GetMonitorInfoW`, `rcWork`), subtract the
  frame `AdjustWindowRect` reports for `STYLE` at 96 DPI (it is approximate, which is why the
  measure-then-correct step stays), and return the largest `s ≥ 1` with
  `1280·s ≤ workWidth − frameWidth` and `720·s ≤ workHeight − frameHeight`. State in a comment what
  this gives on the three panels: 1080p → 1, 1440p at 100% → 1 (does not fit under the taskbar),
  4K → 2 at any DPI.
- `Startup` gains `std::uint32_t scale = 0;` from `--scale <n>`; 0 means choose. `n` is clamped
  to what fits; a value that does not fit is a `DebugTrace` line and the largest that does. This is
  what `Screenshot.ps1` will pass, and it is the only new flag.
- `CreateMainWindow` takes the client size as arguments instead of `CLIENT_WIDTH`/`CLIENT_HEIGHT`
  and keeps the create-measure-correct dance unchanged.
- `RunGame` receives the `Presentation` (built with `Presentation::For(clientWidth, clientHeight, scale)`)
  and passes it to `device.Create` (the client size, which is what `Device::Create` already asks
  for) and to every `screen.Present`.
- `WM_DPICHANGED` is **not** handled; `DefWindowProcW` leaves the window its physical size. Say so
  in a comment beside `SetProcessDpiAwarenessContext`, and cite ADR-075's open question.
- `Build/Screenshot.ps1`: pass `--scale 1` to the executable. Nothing else in the script changes,
  because the client area at scale 1 is still exactly the canvas.

**Verification.** Checkers, builds, suites. Join-screen capture at `--scale 1` byte-identical to
stage 1's. On a monitor with room, the window is 2560×1440 and the picture is a crisp 2× of the
canvas with no letterbox; with `--scale 1` it is the old window. On a 1080p monitor nothing
changed. Drag the window between two monitors of different DPI and confirm it neither resizes
nor blurs (DPI awareness is per-monitor V2, so the compositor does not stretch it).

**Stage 2, as run (2026-09-13).** Three checkers green, both configurations build, 527 tests pass,
and the join screen at `--scale 1` is byte-identical (caret-on hash, as stage 1 defines it).
`--scale 2` on this machine reports `Window: --scale 2 does not fit this monitor; using 1.` and
gives the 1280x720 window, which is the clamp doing what it says.

**This machine cannot see the change, and that is the finding.** The primary monitor is 1920x1080
with a 1920x1020 work area, so `ChooseScale` returns 1 and the window is the window it has always
been. **The letterbox and the magnification are therefore code that stage 2 ships without ever
running it**, which is not a state to leave unverified, so both were photographed from a temporary
build that let `--scale` exceed what fits and let the window be forced to a size that is not a
multiple of the canvas. Reverted immediately afterwards; it is not in the tree.

| forced | client area | what it proves |
|---|---|---|
| `--scale 1 --window 1920 1020` | 1920x1020 | Canvas centred at **(320, 150)** exactly. Every sampled pixel outside the canvas rectangle is pure black, and the 921,600 pixels inside it are **identical** to the scale-1 capture. |
| `--scale 2` | 1924x1055 (Windows clamped the 2560x1440 request to the monitor) | `capture(x, y) == canvas(x/2, y/2)` for every pixel but the caret's, so the magnification is an exact nearest-neighbour 2x and the too-small-surface branch of `Presentation::For` gives a zero offset rather than an underflow. |

The 28 pixels that did differ in the second row are `(492..498, 349)` doubled — the blinking
caret again, four pixels per canvas pixel. It is the same seven pixels stage 1 recorded.

**A deviation, and a correction from the gate.** `ChooseScale` takes no `HINSTANCE`: it needs the
window STYLE and nothing else, so `WINDOW_STYLE` moved to namespace scope and the function takes
nothing. And the loop the plan describes — multiply the canvas up until it stops fitting — was
written that way and `clang-tidy` rejected it: `bugprone-misplaced-widening-cast`, because
`1280 * (scale + 1)` is computed in `unsigned int` and only then widened. It is now two divisions
and a `std::min`, which cannot overflow and is shorter.

**`RunGame` builds the `Presentation` from `GetClientRect` rather than from what was asked for.**
`CreateMainWindow` already measures and corrects, so the client area Windows actually gave is the
one number the swap chain, the letterbox and the pointer must agree on — and as the `--scale 2`
row above shows, Windows does not always give what was asked.

**Commit:** `Size the window to the largest whole scale the monitor has room for`

---

## Stage 3 — A pointer lands on the canvas, not on the surface

**What changes.** `NeuronClient/PointerInput.h` / `.cpp`, one test file.

- `PointerInput::Create(HWND, const Presentation&)` stores the presentation by value.
- `ScreenToClientPixels` becomes `ScreenToCanvasPixels`: after `ScreenToClient`, it calls
  `m_presentation.ToCanvas`. On `WM_POINTERDOWN` a contact outside the canvas is not added and the
  message is not consumed (return `false`). On `WM_POINTERUPDATE` and `WM_POINTERUP` for a
  contact that already exists, the position is mapped **without** the bounds check — a drag that
  leaves the canvas still has to end cleanly — so `ToCanvas`'s return value is ignored there and
  the coordinates are used as they come. `WM_MOUSEMOVE` hover maps the same way and sets
  `m_hasPointer = false` outside the canvas. Wheel is unaffected.
- Everything downstream — contacts, pinch separation, tap cancellation, `TakeClick`,
  `PointerPosition` — is now in canvas pixels, which means the pinch threshold and the tap slop
  are the same number of *canvas* pixels at every scale. Note that in the header: it is the
  property that makes the gesture core reusable on a phone.

`Tests/NeuronClientTests`, `PresentationTests`: `ToCanvas` at scale 1 is the identity; at
`For(2560,1440,2)`, `(2559, 1439)` maps to `(1279.5, 719.5)` and is inside; at `For(1920,1080,1)`
a point at `(10, 10)` is in the letterbox and returns false, and `(320, 180)` maps to `(0, 0)`.
Sub-pixel: keep the division in `float` so a 2× surface still reports half-canvas-pixel
positions; the pages already take `float`.

**Verification.** Checkers, builds, suites (the existing `PointerInput` tests against a real HWND
run at scale 1 and must still pass unchanged). Manually at scale 2: every button on the join and
seats screens presses where it is drawn; a click in the letterbox does nothing.

**Stage 3, as run (2026-09-13).** Three checkers green, both builds, 532 tests pass (five new), and
the join screen at `--scale 1` is byte-identical. The existing `PointerInput` tests against a real
HWND pass with their assertions untouched.

**The bounds check is split rather than passed through, and that shape is the stage.**
`ScreenToCanvasPixels` returns false only when `ScreenToClient` itself failed; whether the mapped
point is ON the canvas is a second question, `Presentation::OnCanvas`, asked only where it matters.
`WM_POINTERDOWN` asks it and refuses a press in the letterbox — no contact, and not consumed, so
the window's default handling still happens. `WM_POINTERUPDATE` deliberately does not ask: a finger
that started on the canvas and dragged into the letterbox is still dragging, and the coordinates are
used negative. Putting the verdict in `ToCanvas`'s return and ignoring it at one call site would
have read as an oversight rather than as a decision.

**Verified end to end at a presentation that is not the identity**, because this machine's 1080p
monitor chooses scale 1 and would otherwise exercise none of it. From the same temporary
`--window` build stage 2 used, at a 1920x1020 client area with the canvas at (320, 150):

- A real `SendInput` click at **canvas (826, 479)** — surface (1146, 629) — pressed JOIN and the
  client joined the hosted match. The button was pressed where it is DRAWN, through the whole chain:
  window, `ScreenToClient`, `ToCanvas`, `TakeClick`, the page's hit list.
- A real click at surface (10, 10), out in the black, changed **seven pixels** — `(492..498, 349)`,
  the caret — and nothing else.

**`PointerCanvasTests` is the same two facts without a GPU**, over a real 1920x1080 `WS_POPUP`
window and real `WM_POINTER*` lParams: a tap reports the canvas pixel it landed on, canvas (0, 0) is
not surface (0, 0), a press in the letterbox is neither recorded nor consumed, a drag that leaves
the canvas keeps dragging with a negative canvas x, and hover in the letterbox is no hover at all.

**One asymmetry, left alone deliberately.** A `WM_POINTERUP` is consumed even for a press that
began in the letterbox and was therefore never recorded. `RemoveContact` on an unknown id is a
no-op, so nothing observable follows from it; making it symmetric would mean remembering which
pointer ids were accepted, which is state bought for no behaviour.

**A flaky gate, and it is not this plan's.** `GameLogicTests::ATickResolvesFastEnoughToReplayAWholeMatch`
asserts that a whole match replays in about a second, and it FAILED at 9 s while `clang-tidy` was
saturating this machine — then passed at 609 ms on an idle one, with the whole five-suite run
going from 8.6 minutes to 3.2. It is a wall-clock assertion on a shared machine, so it will do
this again to whoever runs the gate beside a build.

**Commit:** `Map a pointer from the surface onto the canvas`

---

## Stage 4 — The recorder is the only append path

**What changes.** `NeuronClient/ShapeRenderer.h` / `.cpp`, `NeuronClient/FontRenderer.h` / `.cpp`,
three test files.

Today each renderer has two append paths behind one `m_headless` flag: a mapped upload heap, or a
vector. Make the vector the only path.

- `ShapeRenderer`: `m_headlessVertices` becomes `std::vector<ShapeVertex> m_vertices`, reserved to
  `MAX_VERTICES_PER_FRAME` once, cleared in `BeginFrame`. `AppendShadedTriangle` pushes into it and
  keeps its overrun `ASSERT_TEXT`. `Flush` copies `[m_flushedThisFrame, m_usedThisFrame)` into the
  mapped slice for `m_frameIndex` (the upload heap keeps its `FRAME_COUNT` slices — that part is
  unchanged and still what stops the CPU overwriting a frame in flight) and draws that range.
  `CreateHeadless()` and `m_headless` are deleted: a renderer that has not been `Create`d simply
  records, and `Flush` asserts `m_pipeline != nullptr` with the message the headless assertion
  has today. Delete the `m_mappedVertices`-as-vector trick.
- `FontRenderer`: the same for `TextVertex`. `m_headlessStrings` becomes `m_drawnStrings`, always
  recorded, cleared in `BeginFrame`; `DrawnStrings()` keeps its name. This costs a few hundred
  small string copies per drawn frame on a client that redraws only when something changed
  (ADR-047); it is not worth a second code path, and it is what `FaceRuleTests` reads.
- `Tests/LockstepTests/TapTests.cpp`, `JoinPageTests.cpp`, `FaceRuleTests.cpp`: delete the six
  `CreateHeadless()` lines. Nothing else in the tests changes, which is the point — what a test
  drives is now literally what ships, not a sibling of it.

**Verification.** Checkers, builds, suites. Join-screen capture at `--scale 1` byte-identical.
In the Debug build, the shape budget assertion still fires if `MAX_VERTICES_PER_FRAME` is
temporarily set to 64 (do this once, by hand, and put it back).

**Stage 4, as run (2026-09-13).** Three checkers green, both builds, 532 tests pass, and the join
screen at `--scale 1` is byte-identical. With `MAX_VERTICES_PER_FRAME` temporarily set to 64 the
budget assertion still fires, and with its own message — read out of the composition root's
message box: *More interface geometry in one frame than
ShapeRenderer::MAX_VERTICES_PER_FRAME allows.* Put back immediately.

**The upload heap keeps its FRAME_COUNT slices and `Flush` copies only the UNFLUSHED range into
one.** Copying the whole frame each time would have been simpler and wrong in a way nothing would
have caught: a frame flushes three times (world, interface, dialog), so the first layer would be
copied three times for no reason, and the cost would grow with the number of layers rather than
with the geometry.

**`BeginFrame` stopped being `noexcept`, in both renderers, and `clang-tidy` is what noticed.** It
reserves on its first call, and `bugprone-exception-escape` is right that an allocation failure
escaping a `noexcept` function is a `std::terminate` with nothing to report — which is the one
thing `Debug.h` exists to prevent. `ShapeRenderer` was changed by hand and `FontRenderer` was
missed; the gate caught the half that was missed.

**A false alarm worth writing down, because it cost a stash and a bisect.** After the budget
experiment, 38 tests failed with the shape-budget assertion firing out of `Starfield::Draw`. It was
not this stage: `MAX_VERTICES_PER_FRAME = 64` was still baked into `LockstepTests.obj`, because
restoring the header and running an incremental build did not rebuild the test DLL that includes
it. **A `/t:Rebuild` after any experiment that edits a header**, or the next hour goes into a bug
that is not there. The evidence that settled it was `git stash` + the same single test passing on
the previous commit, then failing again — which pointed at the build rather than the diff.

**The remaining flake is `GameLogicTests::ATickResolvesFastEnoughToReplayAWholeMatch` and it is not
this plan's.** It asserts a whole match replays in under 2000 ms. Measured on this machine:
**609 ms** run alone, **870–978 ms** in a quiet five-suite run, **2228 ms** and **~9 s** when
something else was building. CI runs all five suites in one `vstest` invocation
(`.github/workflows/build.yml`), which is the arrangement that produced the slow numbers here, so
this will redden a busy runner. Nothing in RENDER-01 touches `GameLogic`.

**Commit:** `Record every vertex before drawing any of them`

---

## Stage 5 — The recorder's header names no graphics API

**What changes.** The split proper. Each renderer becomes a recorder with no D3D12 in its header
and a backend that drains it.

- `NeuronClient/ShapeRenderer.h`: keep every public drawing method, `BeginFrame`, the constants
  and `SegmentsForRadius`. Remove `Create`, `Flush`, and every `winrt::com_ptr`, `ID3D12*` and
  `Device.h` include. `ShapeVertex` becomes a **public** nested aggregate (the backend reads it;
  the header's R8 comment already says it is "a public aggregate handed to the GPU"). Add
  `[[nodiscard]] std::span<const ShapeVertex> TakeUnflushed() noexcept` which returns the range
  since the last take and advances `m_flushedThisFrame`. The header's only includes are `Color.h`
  and the standard library.
- `NeuronClient/ShapeBackend.h` / `.cpp` (new, D3D12): `Create(ID3D12Device*)`,
  `Draw(ID3D12GraphicsCommandList*, std::uint32_t _frameIndex, ShapeRenderer& _recorder)` — takes
  the unflushed span, copies it into slice `_frameIndex` of the upload heap, and issues the draw.
  The root signature, pipeline, vertex buffer and `CompiledShaders/Shape*.h` includes move here.
- `FontRenderer.h` / `FontBackend.h` likewise. The recorder keeps the atlas *data* (`Font.h`,
  `AtlasTexel`, metrics, wrapping, UTF-8, the clip rect, `DrawText`, `DrawnStrings`); the backend
  keeps the atlas *resource*, the SRV slot, the pipeline and `Flush`'s body as `Draw`.
  `TextVertex` becomes public.
- `Lockstep/Lockstep.cpp`: `Neuron::ShapeBackend shapeBackend; shapeBackend.Create(device.Handle());`
  and `Neuron::FontBackend textBackend; textBackend.Create(device, shaderVisibleHeap);`. Every
  `shapes.Flush(commandList)` becomes `shapeBackend.Draw(commandList, device.FrameIndex(), shapes)`,
  and the same for text. The pages and `MapRender` are untouched: they take `ShapeRenderer&` and
  `FontRenderer&` and those types still exist with the same drawing API.
- Register the two new `.cpp`/`.h` pairs in `NeuronClient.vcxproj` and `.filters`.
- `Tests/LockstepTests` and `Tests/NeuronClientTests` link `NeuronClient` as before; they never
  needed a backend and now cannot reach one.

**Verification.** Checkers, builds, suites. Join-screen capture at `--scale 1` byte-identical.
And the check this stage exists for:

```
grep -lE "ID3D12|DXGI|winrt::|D3D12_|HWND" NeuronClient/ShapeRenderer.h NeuronClient/FontRenderer.h NeuronClient/Presentation.h
```

returns nothing. (The `pch.h → NeuronClient.h` umbrella still parses `<d3d12.h>` into every
translation unit; that is the include-graph cut `mobile-portability.md` §3 describes, and it is
not this plan's.)

**Commit:** `Split each renderer into a recorder and a Direct3D 12 backend`

---

## Stage 6 — The text shader writes nothing instead of discarding

**What changes.** `NeuronClient/Shaders/TextPS.hlsl` only. Replace the `discard` branch with an
early `return float4(_input.color.rgb, 0.0)`. Under the text pipeline's blend state
(`SRC_ALPHA / INV_SRC_ALPHA` on colour) an alpha of zero leaves the destination colour exactly as
it was in `UNORM8` arithmetic, so the canvas's RGB is unchanged; the canvas's alpha becomes zero
on those pixels, which ADR-075 records as inert and which `CanvasPS` overwrites with 1. Update
the shader's opening comment: `discard` forces late depth on a tile-based mobile GPU and the
text pass has depth disabled, so it bought nothing and is a trap for a second backend.

**Verification.** Checkers, builds, suites. Join-screen capture at `--scale 1` byte-identical.
If it is not, the cause is the alpha arithmetic above and it must be understood before this stage
lands; do not "fix" it by widening the change.

**Commit:** `Let the text shader write nothing rather than discard`

---

## Stage 7 — Borderless fullscreen (owner decision; not started unless the owner says so)

**Why it is a stage and why it is last.** It is the only way a 1440p panel at 100% ever sees
scale 2, and the only path in this plan that resizes a swap chain. Everything else works without
it, so it waits for a yes.

If yes: `F11` toggles between the stage 2 window and a `WS_POPUP` window covering the monitor
(`MonitorFromWindow`, `rcMonitor`); the scale is recomputed against the *monitor*, not the work
area; `Device` gains `Resize(width, height)` — `WaitForGpu`, release the back buffers,
`ResizeBuffers`, recreate the RTVs, reset the fence values — and the `Presentation` is rebuilt and
handed back to `PointerInput`. The letterbox already handles a monitor that is not a multiple of
the canvas.

**Verification.** On a 1080p monitor fullscreen is scale 1 with a 320×180 border on each side; on
1440p it is scale 2 exactly; toggling ten times in a row leaves `DrainDebugMessages` silent.

**Commit:** `Toggle borderless fullscreen`

---

## How every stage is checked

The same gate, every time, in this order, and the stage does not commit until all of it is green:

1. `py Build\CheckFormat.py`, `py Build\CheckProjectFiles.py`, `py Build\RunClangTidy.py` (the last
   from a Developer PowerShell).
2. `msbuild Lockstep.slnx /p:Configuration=Debug /p:Platform=x64` and the same for Release —
   AGENTS.md §3 says Release is where a Debug-only `FXCompile` option fails, and stage 1 adds two.
3. `vstest.console.exe` on all five suites, as `.github/workflows/build.yml` does.
4. `powershell.exe -ExecutionPolicy Bypass -File Build\Screenshot.ps1 -Exe x64\Debug\Lockstep.exe -Out shot.png`
   on the join screen, then `Get-FileHash shot.png` against the hash recorded before stage 1.
   FONT-01 §Stage 0 says why the join screen and not the main page: the main page advances on a
   schedule and two captures are never at the same tick.
5. In the Debug build, `DrainDebugMessages` prints nothing.

Write what each stage found under its heading before committing, as FONT-01 did — the stages
that found nothing say so. A stage that has to deviate from what is written here says why in
its commit and in this file, never silently.

## What is left when this plan is done

The desktop client draws into a canvas, presents it at a whole scale, and takes its pointer in
canvas pixels; its two renderers record into plain vectors that a D3D12 backend drains; nothing
a page or a test includes names a graphics API. What the mobile client still needs, none of it
started here: a portrait canvas of `floor(surface / round(density))` logical pixels with a layout
relative to its edges; a lifecycle in which a lost surface is routine; a decoder from
`AMotionEvent` or `UITouch` into the gesture core; a Vulkan backend behind the same recorder, and
a Metal one; a build that is not MSBuild; and a test harness that is not `CppUnitTest`. Each is
its own plan, and the first of them is the one the blueprint owes: the ADR revising the baseline
for a portrait canvas.
