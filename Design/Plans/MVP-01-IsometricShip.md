# MVP-01 — An isometric ship you can send somewhere

**Status:** Plan, not started. Owner decisions recorded 2026-09-09, in two rounds: the first settled input, the ship, authority and the palette; the second settled the shader variable naming, the camera, the tick rate and threading. §2 is the complete list.
**Read first:** [`AGENTS.md`](../../AGENTS.md) in full, then [`Design/README.md`](../README.md) §1 (the baseline). This plan does not repeat either; it depends on both.

This is the prompt for the first build session, kept in the tree so that the session updates it rather than losing it. When the work is done, move this file to `Design/Archive/` and leave behind the ADRs it produced.

---

## 1. What "done" looks like

`FrontierOutpost.exe` starts, opens a fixed 1280×800 window, and shows a **640×400, 16-colour** isometric view of one space ship drifting in space. **Tap or click anywhere on the ground plane and the ship turns toward that point, travels to it, slows, and stops.** A line of 8×8 text in the corner (from `NeuronClient/Font.h`) shows the tick count and the ship's position. That is the whole feature.

It is small on purpose. What it buys is not the ship — it is that **every layer the game will ever have exists and is exercised once**:

```
touch/mouse ─► client ─► loopback transport ─► server (GameLogic ticks the ship)
                  ▲                                     │
                  └──── replicated ship state ◄──────────┘
                  │
            D3D12: mesh ─► 640x400 index target ─► palette ─► 2x blit ─► window
```

Nothing may shortcut that arrow. The client never moves the ship. The server never draws.

## 2. Decisions already made (do not reopen these)

| | Decision | Where it binds |
|---|---|---|
| **Input** | RTS point-and-click. **Touch is primary; mouse is the fallback when no touch is present. No keyboard control at all.** One code path for both through the Windows Pointer API (`WM_POINTER*`, with `EnableMouseInPointer(TRUE)` so a mouse arrives as a pointer). | Owner, 2026-09-09 |
| **The ship** | A **simple 3D mesh** — a hull a dozen or two triangles long, embedded as `constexpr` vertex/index arrays — rendered by D3D12 with an isometric camera. Not a sprite. | Owner, 2026-09-09 |
| **Authority** | **Server-authoritative from day one.** The click becomes an order; the order crosses an in-process loopback transport; `GameLogic` ticks the ship on a fixed tick; the client renders replicated state and never simulates. | Owner, 2026-09-09 |
| **Palette** | **EGA default 16**: `000000 0000AA 00AA00 00AAAA AA0000 AA00AA AA5500 AAAAAA 555555 5555FF 55FF55 55FFFF FF5555 FF55FF FFFF55 FFFFFF`, indices 0–15 in that order. | Owner, 2026-09-09 |
| **Presentation** | 640×400 virtual screen, integer scale 2, no filtering anywhere on the path from index buffer to window. | `Design/README.md` §1 |
| **Shaders** | Compiled at build time, `<Library>/Shaders/<Shader>VS.hlsl` → `<Library>/CompiledShaders/<Shader>VS.h`. The array is named for the **file stem**: `g_PaletteResolveVS` and `g_PaletteResolvePS`, which is what a single `FXCompile` rule with `/Vn g_%(Filename)` produces. No underscore before the stage, no per-file naming. | Owner, 2026-09-09; `AGENTS.md` §2, R13 |
| **No assets on disk** | Everything embedded. `Font.h` is the pattern. | R13 |
| **Camera** | **Follows the ship.** The ship stays centred on the 640×400 screen and space scrolls under it. Space is unbounded; there is no play-area clamp and the ship can never leave the screen. A click is therefore always a point on the plane, offset from the ship. | Owner, 2026-09-09 |
| **Tick** | **20 Hz** — a 50 ms server tick, fixed. Recorded as a decision, not a measurement; the client interpolates between the last two states. | Owner, 2026-09-09 |
| **Threading** | **The server has its own thread from the first commit.** The loopback transport is two thread-safe queues, one each way; the client thread never blocks on the server and the server never touches D3D12. Same-thread is not an option, not even "for now". | Owner, 2026-09-09 |

## 3. What you must decide, and record as ADRs

Each of these has real alternatives. Pick one, write it up with [`Design/ADR/ADR-000-template.md`](../ADR/ADR-000-template.md), number from `ADR-001`, and cite the ADR from the code that implements it. Recommendations are given; you may depart from one, in the ADR, with a reason.

**ADR — How 16 colours reach the screen.** *Recommended:* a 640×400 `R8_UINT` render target holding palette indices, with a matching depth buffer; every pass writes an index; one fullscreen resolve pass reads the index, looks up the palette, and writes the 1280×800 swap-chain back buffer with point sampling. The alternative — render RGBA and quantise — puts the palette decision in the wrong place and blurs the edges the legacy look depends on. State which you chose and why the other loses.

**ADR — How a lit mesh becomes 16 colours.** *Recommended:* flat shading, one palette index per face as authored, and the light chooses between the dark and bright variant of that colour (EGA's index `n` and `n+8`) — two tones, period-correct, no dithering. Nearest-colour matching of a continuous lit value is the alternative; say why not.

**ADR — The isometric camera.** *Recommended:* orthographic, 2:1 dimetric (yaw 45°, pitch `atan(0.5)` ≈ 26.565°), the projection pixel-art isometric games actually used. True isometric (35.264°) is the alternative. The camera **follows the ship** (§2), so the view matrix is a translation by the ship's interpolated position and the projection never changes; record the exact matrix and the world-units-per-pixel it implies, because the pick-ray in §4 depends on it. Decide whether the camera snaps to whole virtual pixels as it follows — it should, or the starfield swims — and say so.

**ADR — Ship kinematics on the server.** The tick is **20 Hz** (§2); this ADR is about what happens inside one. *Recommended:* turn-rate-limited heading, acceleration to a max speed, deceleration to arrive on the target with the ship stopped. Integers or fixed-point for position (R16); state the unit and the range — space is unbounded (§2), so say what the integer does at the edge of its range and why that is not reachable in practice. The alternative is "teleport along a straight line", which is what the MVP would look like if nobody decided.

**ADR — Replication and interpolation.** What the server sends each tick, how big it is, and how the client renders between ticks (interpolate the last two states; do not extrapolate). Say what happens on the first frame before any state has arrived, and what the camera does then.

**ADR — The transport's queues.** Threading itself is decided (§2: own thread). What this ADR settles is the queue: lock-free single-producer/single-consumer ring, or a mutex and a `std::deque`; the capacity; and what happens when it is full — drop the oldest order, drop the newest, or block. State the choice and the reason; the MVP will never fill it, and the MMO will.

## 4. The work, in order

Each step ends green: builds, four suites pass, three checkers pass, and the executable was **run**. Do not start the next step on a red one. Commit at each step boundary.

### Step 0 — Read, then plan out loud

Read `AGENTS.md`, `Design/README.md`, the current `NeuronCore.h`, `Debug.h`, `Font.h` and `FrontierOutpost.cpp`. Report what is there, what §2 constrains, and which ADRs you will write — before writing code. If anything in this plan contradicts the tree, say so and stop.

### Step 1 — The device and the window that shows nothing

`NeuronClient`: a `Device` (or whatever R2 lets you call it) owning the `ID3D12Device`, a direct queue, a triple-buffered flip-model swap chain on the existing window, fences, and the per-frame command allocator/list dance. Debug layer on in `_DEBUG`. The window is already 1280×800 client area in `FrontierOutpost.cpp`; keep that and remove the `WM_PAINT` handler — the swap chain owns the pixels. Present a cleared back buffer.

Run it. A black window with no D3D12 debug-layer output is the exit criterion. **Handle device removal** at least to the extent of failing loudly through `Debug.h` rather than presenting garbage.

### Step 2 — The 640×400 palette target and the resolve pass

Per your first ADR. `NeuronClient/Shaders/PaletteResolveVS.hlsl` and `PaletteResolvePS.hlsl`, compiled by an `FXCompile` item you add to `NeuronClient.vcxproj` (Shader Model 6.0, header output `$(ProjectDir)CompiledShaders\%(Filename).h`, variable `g_%(Filename)`, no object output). Register the `.hlsl` in the `.vcxproj` **and** the `.filters`. Extend `Build/CheckProjectFiles.py` so that `.hlsl` under `Shaders/` is registration-checked the way `.cpp` is — this is the first time the checker meets a subdirectory, so the check is yours to add.

The palette is a `constexpr std::array<std::uint32_t, 16>` in a `NeuronClient` header, R3-named, with a unit test in `NeuronClientTests` that pins all sixteen values. Fill the index target with index 1 (blue) and resolve it. Run it: a blue window, 1280×800, and you can see there is no filtering by taking a screenshot and checking a pixel boundary is hard.

### Step 3 — Text

A `FontRenderer` (or similar) that turns `FONT_DATA` into an 8-bit texture once at startup and draws a string as textured quads into the index target with a given palette index, `TextVS`/`TextPS`. Draw `"FRONTIER OUTPOST"` at (8, 8) in index 15. Unit-test the glyph lookup (which 8 bytes are `'A'`) in `NeuronClientTests`.

`Font.h` currently fails `CheckFormat.py`; run `--fix` on it as part of this step, since you are now its first consumer.

### Step 4 — The mesh, lit, isometric, still

Per the camera and shading ADRs. `MeshVS`/`MeshPS`. The hull is a `constexpr` vertex and index array in **`FrontierOutpost/`** — it is *game* content (R9: the engine does not know what a ship is), in `namespace Frontier`; `NeuronClient` gets a generic "draw this mesh with this world matrix through this camera" and nothing ship-shaped. A root signature with one constant buffer for view-projection and one for the world matrix; depth on. Run it: a ship sitting at the origin, faces two-toned, edges crisp at 2×.

### Step 5 — The server, the tick, the order

`GameLogic`: a `Ship` with position, heading, speed; a `MoveToOrder`; a `World::Tick()` that advances one ship per the kinematics ADR. **Unit-test the kinematics in `GameLogicTests`** — turning toward a target, arriving stopped, not overshooting — with no D3D12, no window, no threads. This is the test suite that will matter most for the rest of the game's life; make it the best one.

`NeuronCore`: the wire records for `MoveToOrder` and `ShipState`, fixed layout, sizes asserted at compile time, serialised to and from bytes with a round-trip test in `NeuronCoreTests`.

`NeuronServer`: a `Session` that owns a `World`, receives orders from a transport, ticks on the schedule from the threading ADR, and pushes `ShipState` back. `NeuronCore` provides the `LoopbackTransport` (R2: no `I`, no `Base`), tested in `NeuronCoreTests` with two ends in one process.

`FrontierOutpost.cpp` starts the session, wires the loopback ends, and the client renders whatever `ShipState` it last received (interpolated per the ADR). Nothing moves yet, because nothing has sent an order. Run it anyway: the ship is still at the origin, but now it got there through the server.

### Step 6 — The click

Pointer input in `NeuronClient`: `WM_POINTERDOWN` (and `WM_LBUTTONDOWN` only if you have proven `EnableMouseInPointer` is unavailable — say so). Physical window coordinates ÷ 2 → virtual 640×400 → un-project through the camera ADR onto the `y = 0` plane → a world point **relative to the camera, which is relative to the ship** (§2: the camera follows) → a `MoveToOrder` in world coordinates onto the transport. Unit-test the un-projection in `NeuronClientTests`: the centre of the screen maps to the ship's position; a known pixel maps to a known offset from it; the same pixel after the ship has moved maps to a different world point by exactly the ship's displacement.

Run it, **with a finger if the machine has touch, with a mouse if not, and say which.** The ship turns, travels, and stops on the point. The text line shows tick and position and they agree with what you see.

### Step 7 — Close out

- Every ADR from §3 written and cited from code.
- `SuiteSmoke` deleted from any suite that now has a real test (AGENTS.md §3 says when).
- This plan moved to `Design/Archive/MVP-01-IsometricShip.md` with a short "what shipped, what did not" section at the top, dated.
- Report per AGENTS.md §7 and `Design/README.md` §6: what was built, what was run and how, every rule bent, everything you noticed and left alone.

## 5. Things that will tempt you, and the answer

- **"I'll just move the ship on the client to see it work."** No. Step 5 before Step 6 exists so that the first movement ever seen is server-driven. A client-side stub is the seam that never gets removed.
- **"The keyboard would be handy for testing."** No keyboard control. If you need a debug affordance, it is a second click target, not a key.
- **`D3DCompile` at startup** because the build-time step is fiddly. No: R13. Get the `FXCompile` item right; it is a few lines of MSBuild and the pattern is then set for every shader after.
- **Bilinear anything.** Point sampling on the resolve, integer scale, no MSAA, no smoothing. If it looks blurry it is wrong.
- **Putting the ship in `NeuronClient` because that is where the renderer is.** The renderer draws meshes; the game owns the ship (R9).
- **A `float` position in `GameLogic`** because it is easier. R16. Fixed-point or integer, unit in the name.
- **Reaching for DirectXTK12, DirectXMath helpers from a NuGet, or any package.** R14. `DirectXMath` from the Windows SDK is fine; nothing that is not already on the machine is.
- **Skipping the run.** Every step says "run it". A green build of a renderer proves nothing.

## 6. Open questions this plan leaves to the session

- Whether the depth buffer at 640×400 needs a bias to keep the hull's silhouette clean at 2:1 — find out at Step 4 and record it in the camera ADR.
- Whether 20 Hz *looks* right under interpolation. The number is decided (§2), not up for a vote; but if it reads badly at Step 6, say so in the report with what you saw, rather than quietly changing it.

Two questions this plan used to leave open are closed by §2 and are listed so nobody reopens them: a click cannot miss the plane (orthographic camera, no horizon, unbounded space), and the server does not get to run on the client thread.
