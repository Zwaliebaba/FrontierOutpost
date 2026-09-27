# ADR-017: The game runs borderless fullscreen

- **Status:** Accepted (owner, 2026-09-27)
- **Scope:** how `FrontierOutpost.exe` opens its window: `Neuron::Window::Desc::fullscreen` in
  `NeuronClient/Window.h`, `Window_Create` in `NeuronClient/LteWindow.cpp`, and the launcher in
  `FrontierOutpost/Main.cpp`
- **Amends:** ADR-012 decision 6 and "What this forecloses"

## Context

ADR-012 kept what SFML gave the game: a 1920×1080 client area in a framed window, with borderless
fullscreen left in the backlog. On a 1920×1080 display that window, frame included, is larger than
the screen, so its title bar and edges hang off it. Everything that draws already follows the
client area: the frame texture (`Renderer_GetFrame`), the viewport and the swap chain take its size,
and the swap chain resizes on a `ResizeEvent`. DXGI's exclusive fullscreen was never supported, and
the swap chain turns off Alt+Enter so that DXGI cannot enter it (ADR-007).

## Decision

1. **A player's run is borderless fullscreen.** The window is a `WS_POPUP` covering the whole of the
   primary display, made at that size, so the game draws at the display's resolution. There is no
   frame, and there is no switch to a window and back (owner, 2026-09-27).
2. **It is never DXGI's exclusive mode.** A flip-model swap chain over a popup that covers its
   display is presented by DWM through independent flip where the driver allows it, which is what
   exclusive mode was for. Alt+Enter stays off.
3. **The smoke mode and WARP keep the 1920×1080 window.** A run with `--frames` or `--warp` opens
   framed at 1920×1080, as before, so its captures stay that size on any display and compare with
   the earlier ones. WARP draws offscreen and shows nothing, so a fullscreen popup would only cover
   the screen.
4. **The policy lives in `Main.cpp`,** which decides the mode from the arguments. NeuronClient's
   `Window` only has the mechanism: `Desc::fullscreen`, and `WidthPixels()`/`HeightPixels()`, which
   `Window_Create` reads so that the engine starts at the size it really got.
5. **Nothing else changes.** Close and Alt+F4 still quit (ADR-012 decision 3), DPI awareness stays
   system-wide (decision 4), and the OS cursor stays hidden.

## What this forecloses

- **A framed window for a player's run,** and any key or argument that switches to one. Bringing
  one back is a new decision.
- **Choosing the display.** Fullscreen is the primary display; a second monitor is not offered.
- **A fixed render size.** On a display other than 1920×1080 the game draws at that display's size,
  as it would have in a resized window. It was seen at 1920×1080 only (see below).

## Verification

Measured on 2026-09-27, on the owner's machine (Intel Iris Xe, one 1920×1080 display at 125%):
`war` opened as a popup with no caption at (0,0)–(1920,1080), a 1920×1080 client area, drew as
before and closed on `WM_CLOSE`. `war --frames 60 --capture` still wrote a 1920×1080 PNG.
`Tests/NeuronClientTests/WindowEvents.cpp` checks that a fullscreen window covers the primary
display without a frame, and that `WidthPixels()`/`HeightPixels()` report the size it opened at.
