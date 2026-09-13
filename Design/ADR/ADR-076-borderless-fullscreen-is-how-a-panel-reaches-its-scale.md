# ADR-076 — Borderless fullscreen is how a panel reaches the scale it has room for

**Status:** Accepted 2026-09-13 (owner, asking for RENDER-01 stage 7 to be built)

**Date:** 2026-09-13
**Decided by:** Owner decision, 2026-09-13. ADR-075 left "whether borderless fullscreen ships" as an open question, and this answers it.
**Supersedes:** — (answers an open question ADR-075 left; ADR-075 is Accepted and is not edited)

---

## Context

ADR-075 made the screen a 1280×720 canvas presented at the largest whole scale the display has room
for, and recorded a consequence that reads like a defect until you know why it is there:

> **On a 1440p panel at 100%, nothing changes either, and that is worth saying out loud.** A
> 2560×1440 client area does not fit inside a 2560×1440 monitor once a caption bar and a taskbar
> have taken their rows, so the largest scale that fits the *work area* is still 1.

That is the whole of the case. A 2560×1440 panel has room for the canvas at 2× **exactly** — the
numbers are equal, not merely close — and a windowed presentation gives that room away to a title
bar. The owner of such a panel gets a 1280×720 window on a screen that could have been showing them
a 2560×1440 picture, and nothing they can do from inside the game changes it.

The rest of the presentation is already built for this. `Presentation` centres the canvas in a
surface of any size and puts black around it, `PointerInput` maps a pointer back through whatever
presentation it is given, and RENDER-01 stage 2 photographed both working at a client area that was
not a multiple of the canvas. **What is missing is a surface that is the monitor**, and the one
thing this renderer has never done: resize a swap chain.

The constraint that binds is `Design/README.md` §1 again — nothing on the path from a vertex to the
display resamples anything. Whatever fullscreen means here, it cannot mean asking something else to
stretch the picture.

## Options considered

### A. Ship nothing; 1440p stays at scale 1

Costs nothing and is honest: ADR-075 works without this, and every other panel already gets the
scale it has room for.

Rejected because the panel it fails is not an edge case — 2560×1440 is an ordinary desktop monitor,
and the failure is the most visible one the presentation can produce: exactly half the picture the
display could show. It is also the only ADR-075 consequence that a reader would call a bug.

### B. Exclusive fullscreen (`IDXGISwapChain::SetFullscreenState`)

The historical answer, and it takes the display mode with it.

Rejected, and not narrowly. It changes the monitor's mode, which flickers other windows and takes
seconds to come back from; it makes alt-tab a device-lost path this client has no handling for
(`Design/Reference/mobile-portability.md` §7 says the error model is a desktop one already); and on
a flip-model swap chain Windows composites a borderless window at the same cost anyway, which is why
Microsoft's own guidance moved to borderless. It would also be a *second* way of being fullscreen
beside Alt+Enter, which `MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)` has deliberately refused since
ADR-011.

### C. A borderless window covering the monitor, toggled by F11

`WS_POPUP` at the monitor's rectangle, the swap chain resized to match, and the scale recomputed
against the *monitor* rather than the work area. No mode change, no device loss, alt-tab is a
window switch.

The canvas is unchanged at 1280×720; what changes is the scale and the letterbox, both of which
already exist and are already tested.

### D. Borderless fullscreen, but chosen at startup by a flag

Same picture, no toggle, no swap-chain resize — `--fullscreen` and the window is made that way once.

Genuinely cheaper, and rejected on what it asks of the player: a person who wants to see whether the
bigger presentation suits them has to quit the game and start it again with an argument, and a
person who wants their desktop back has to quit. The resize is the only part that costs anything and
it is forty lines.

## Decision

**F11 toggles between the window ADR-075 sizes and a borderless window covering the monitor the
window is on.** Option C. The scale is recomputed for the new surface — against `rcMonitor` when
going fullscreen, against `rcWork` minus the window frame when coming back — the swap chain is
resized to the new client area, the `Presentation` is rebuilt from the client area Windows actually
gave, and `PointerInput` is handed the new one.

**The window's own style is where "am I fullscreen" is kept.** `WS_POPUP` is the whole difference;
a `bool` beside it would be a second answer to a question the window can already be asked.

**The scale arithmetic moves into `Presentation::LargestScaleFor`,** because there are now two
callers with different ideas of what the surface is, and a phone's is a third. It is one divide each
and the smaller answer, never less than 1.

**Exclusive fullscreen stays refused.** `DXGI_MWA_NO_ALT_ENTER` remains, and the comment beside it
now says which fullscreen this game has.

**Nothing about the canvas changes.** It is 1280×720 in every mode, every number in
`Design/UI/DESIGN-GUIDELINES.md` is still a canvas pixel, and a capture is still taken at
`--scale 1` in a window.

## Consequences

**What this makes easy.** The panel ADR-075 could not serve. On 2560×1440 fullscreen is scale 2
exactly, which is the first time this game fills a modern desktop display with a picture it drew.

**What it costs.** One swap-chain resize path, which this renderer did not have: `Device::Resize`
drains the GPU, releases every back buffer, calls `ResizeBuffers`, recreates the render target views
and resets the fence values. The drain is not caution — `ResizeBuffers` fails outright while any
back buffer still has an outstanding reference, and every frame in flight holds one. It costs a
stall of at most `FRAME_COUNT` frames on a keypress that is already changing the whole screen.

**A gesture in progress is abandoned, not remapped.** `PointerInput::SetPresentation` clears the
press, the drag, the pending tap, the hover and the contacts. A drag whose origin was recorded in
the old presentation cannot be continued in the new one; carrying it across would move the camera by
the difference between two coordinate systems.

**On a 1080p panel fullscreen is scale 1 with a 320×180 border on every side**, because 1920×1080
has room for the canvas once and not twice. That is a real outcome and not a failure: it is a
centred picture on a black field, which is what letterboxing looks like when it is doing its job.

**F11 is read in the window procedure and not by `KeyboardInput`.** That class reports "characters
and a handful of named keys" for a text field and says so in its header; which window a canvas is
presented in is not its business. It leaves an unknown `WM_KEYDOWN` unconsumed, which is what makes
this possible without touching it.

**The toggle is a flag the frame loop consumes, not work done in the window procedure.** The
procedure runs re-entrantly from `SetWindowPos`'s own message pump, so resizing a swap chain there
would be resizing while the window is still being resized.

**What it does not change.** `WM_DPICHANGED` is still not handled, so a window dragged to a monitor
of a different DPI still keeps its physical size — ADR-075's first open question, still open. The
scale is still chosen once per surface rather than tracked, and a monitor that changes resolution
under a fullscreen window is not handled.

**What it forecloses.** Nothing. Exclusive fullscreen remains reachable by a later ADR if some
reason to want it appears, and this toggle would be the thing it replaced rather than a thing in its
way.

## What this changes elsewhere

- **Code:** `NeuronClient/Device` gains `Resize`; `NeuronClient/PointerInput` gains
  `SetPresentation`; `NeuronClient/Presentation.h` gains `LargestScaleFor`, which both scale
  choosers now use; `Lockstep.cpp` gains `ChooseFullscreenScale`, `ApplyFullscreenToggle`, an F11
  case in the window procedure, and a mutable `Presentation` threaded through its three frame loops.
- **Design/:** `Design/UI/DESIGN-GUIDELINES.md`'s Frame line said the scale is "chosen at startup",
  which F11 makes untrue; it is rewritten. `Design/README.md` §1's Presentation row already said
  "letterboxed" and needs nothing. ADR-075 is **not** edited — it is Accepted, and this is where its
  open question is answered.
- **AGENTS.md:** no change. R12 says the canvas is presented at a whole-number scale by one resolve
  pass, which is as true in a borderless window as in a framed one.

## Open questions

**Whether the mode should be remembered between runs.** It is not: every run starts windowed. That
needs somewhere to write a preference, and R13 says the client writes nothing — so it is a decision
about the client's first file, not about fullscreen.

**Whether a fullscreen window should follow its monitor's resolution changing.** It does not. The
surface is sized once per toggle, and a resolution change under it leaves a window the wrong size
until F11 is pressed twice.

**Whether a second monitor's presentation should be chosen when the window is dragged to it.** The
fullscreen scale is the window's monitor, decided at the toggle; the windowed scale is still the
primary monitor's, decided at startup. Those two disagree on a mixed-DPI desktop, and the thing that
would settle it is ADR-075's `WM_DPICHANGED` question rather than this one.
