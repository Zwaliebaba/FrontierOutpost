# Mobile portability — what a move to Android or iOS costs this tree

**What this is.** A Reference (`Design/README.md` §2): the measured coupling between this tree and
the platform it is written against, and the constraints a second platform would run into. It
records what is *true* on 2026-09-10, not what has been decided. Nothing here is a decision, and
the three places a decision would be needed are named in §10 rather than taken.

**Measured** on 2026-09-10 on this branch — `main` at `3468dd6`, plus the retirement of all four
`framework.h` files in favour of the `pch.h` → `<Library>.h` shape the tree now uses everywhere —
by classifying every `.cpp` and `.h` outside `Tests/` as platform-bound or not: comments stripped, then matched against the Win32, D3D12, DXGI,
C++/WinRT and MSVC-intrinsic vocabulary the tree actually uses. A file is *portable* here when its
own text contains none of it. That is a statement about the code, not about the include graph —
§3 says where the two differ. Line counts are physical lines including comments, which in this
tree is roughly half of them.

**Not measured, and not claimed:** any figure about how long a port would take, what it would cost
or how it would perform. There is no mobile build of anything here to measure, so every such
number would be an estimate dressed as a fact (`Design/README.md` §3.2).

**Overtaken since it was measured — noted 2026-09-12.** The tree this document classified is mostly
gone, and the figures below describe that tree, not this one. On the day it was measured, ADR-011
replaced the 640×400 sixteen-colour framebuffer with 1280×720 true colour presented 1:1, and ADR-015
deleted the real-time ship, its kinematics and interpolation, the loopback transport, the mesh
renderer, the starfield and `IsometricCamera` — everything §1 calls "the real-time thing in the
tree". The 4X of the one-pager has been built since: `GameLogic` is the six-phase resolver and is
still integer-only (ADR-018); `NeuronCore` has a TCP `Socket` and a `FrameStream`, so §3's "nothing
in the tree calls a socket function" is no longer true; `NeuronServer` is a real server (ADR-028);
`Lockstep.cpp` is the composition root for three roles; the client's screens are a `LockstepClient`
library (ADR-050); there are five test suites, two of them under Address Sanitizer (ADR-048); and
the match screen redraws only when something changed or a fleet is under way (ADR-047, ADR-055), so
§7's 20 Hz thread and vsync starfield no longer exist. The owner answered §1's question on
2026-09-11: the game is the one-pager's, the desktop client is the prototype and mobile is the
product (`Design/blueprint.md` §7 and §9). What still holds is the shape of the argument — the
renderer, the window shell, the build system and the presentation contract do not travel; the
simulation, the protocol and the input core do — and the findings in §4.3, §4.4, §4.5, §6 and §7
that do not depend on which game. **The file tables in §2 and §3 and the arithmetic in §5 must be
re-measured against the current tree before anything is costed from them.** Between here and §10
nothing has been edited; §10 says where its three decisions stand.

---

## 1. The question this document cannot answer

The tree and the design record describe two different games, and which one is being moved decides
almost everything below.

`Design/space-4x-one-pager-v10.md` describes an asynchronous, tick-quantised 4X: a graph of systems
and lanes, four ticks a day at fixed UTC times, six to twelve humans, a three-week season, a digest
as the primary screen. It says in as many words: *"Not real time. Not a coordinate map — there is no
velocity to tune."* Its opening image is someone opening an app after work, and its diplomacy
section is written around the observation that *"strangers click but don't write."* That is a
portrait-orientation phone game in everything but the label.

The tree implements something else: a 20 Hz real-time simulation of a ship in continuous space,
positions in millimetres, acceleration and turn rate per tick, replicated every tick to a client
that interpolates between the last two states (ADR-004, ADR-005), drawn through a 2:1 dimetric
camera into a 640×400 sixteen-colour framebuffer (ADR-001, ADR-003). It is a coordinate map with a
velocity to tune. `Design/Archive/MVP-01-IsometricShip.md` is honest about what it is — a build
session that proved a rendering and replication stack end to end — and the design documents were
added afterwards, in this same commit `3468dd6`.

So "migrate this to mobile" resolves two ways:

If the game is the one in the one-pager, then **there is no port**. Mobile is the primary platform,
the 4X client is a new client, and the useful question about this tree is which parts of it are
worth keeping — which §3 answers, and the answer is a good deal more than nothing. A 20 Hz tick,
client-side interpolation and millimetre kinematics are not among them; a game that resolves four
times a day needs none of the three.

If the game is the real-time thing in the tree, then it is a port, and §4 through §8 are its bill
of materials.

Both readings share §3 and §9. Everything in §4, §5 and §7 is conditional on the second.

---

## 2. What was measured

| Library | Portable | Platform-bound | Bound files |
|---|---:|---:|---|
| `GameLogic` | 321 | 0 | — |
| `NeuronServer` | 176 | 0 | — |
| `NeuronCore` | 719 | 131 | `NeuronCore.h` (61), `Debug.h` (70) |
| `Lockstep` | 535 | 374 | `Lockstep.cpp` (374) |
| `NeuronClient` | 361 | 2157 | everything except `Font.h`, `Palette.h`, `IsometricCamera.{h,cpp}` |
| **Total** | **2112** | **2662** | 44% of the non-test tree is platform-free |

Beside that: 255 lines of HLSL in eight files, and 1798 lines of test across four suites, all of
them written against MSVC's `CppUnitTest`.

The 44% is the least interesting number here. What matters is where the other 56% sits: **almost
all of the platform coupling is in the two libraries that were always going to be replaced**, and
in the shared library it is concentrated in exactly two files, neither of which contains any logic.

---

## 3. What already ports, and why that is a decision rather than luck

`GameLogic` is 321 lines and contains **no `float` and no `double` at all** — verified by matching
the stripped source, not by reading it. Positions are `std::int64_t` millimetres, headings are a
16-bit fixed-point turn, trigonometry is CORDIC in integers, and the square root is digit-by-digit
in base four (`NeuronCore/Trigonometry.cpp`). It reads no wall clock: the tick is the clock.

Until 2026-09-10 it did not reach `<windows.h>` even transitively, which made it the one library
with no platform header anywhere in its include graph. That is no longer true: `GameLogic.h`
includes `NeuronCore.h`, by owner decision on 2026-09-10, so that all four libraries share one
umbrella-header shape. **The determinism argument below is untouched by that** — it rests on the
arithmetic in the source, not on what the precompiled header parses — but the *include graph* of
the library is now Windows-bound exactly as `NeuronServer`'s is, and cutting either loose is the
same one-line change.

That is R16 doing precisely the job it was written for, and the payoff shows up here rather than
where it was aimed. R16 exists so that two builds of the same simulation agree; the side effect is
that the simulation has no dependence on the floating-point behaviour of the compiler that built
it. Moving `GameLogic` from MSVC to Clang on the NDK or on Apple's toolchain is not a
determinism risk, because there is no floating-point arithmetic for the two compilers to disagree
about. Had the kinematics been written in `float`, `/fp:precise` would have had to be matched
against `-ffp-contract=off` and the results compared on real hardware; instead there is nothing to
compare. **This is the single most valuable property the tree has for a second platform, and it was
bought for a different reason.**

`NeuronCore`'s logic ports on the same terms. `Protocol.cpp` serialises field by field with shifts
rather than `memcpy`, explicitly so the format survives a big-endian machine — a machine that does
not exist in this project and now never will, since both mobile targets are little-endian, but the
discipline also means no struct padding or alignment assumption crosses the wire.
`MessageQueue.h` is `std::array`, `std::mutex` and `std::scoped_lock`. `Session.cpp` is
`std::thread`, `std::chrono::steady_clock` and `sleep_until`. All of that compiles on both targets
unchanged.

Two files in `NeuronCore` are the seam, and they are the whole of it:

`NeuronCore.h` is the umbrella: the Windows macro family, `<windows.h>`, `<WinSock2.h>`,
`<unknwn.h>`, `<hstring.h>`, `<restrictederrorinfo.h>`, and `#pragma comment(lib, "ws2_32.lib")`.
Nothing in the tree calls a socket function — `grep` for `socket`, `bind`, `recvfrom`, `WSAStartup`
and their kin returns nothing — so the Winsock half of that header is a placeholder for a network
layer that has not been written. That is good news: the transport that does not exist yet can be
written portable from the first line, against BSD sockets, which both platforms speak.

`Debug.h` is the error path, and it is the more interesting of the two. `OutputDebugStringA`,
`__debugbreak`, `__noop` and `<crtdbg.h>` are all replaceable in an afternoon —
`__builtin_trap`/`__builtin_debugtrap` and `__android_log_print`/`os_log` are the obvious
substitutes. What does not survive is the *shape*: "there is no third error path", one exception
caught at the composition root, a message box, exit. §7 explains why that is a desktop assumption.

`NeuronServer` is 176 lines with no platform token in any of them, but its `NeuronServer.h`
includes `NeuronCore.h`, so every translation unit in it sees `<windows.h>` and `Session.cpp`'s
`ASSERT_TEXT` reaches `Debug.h`. Unlike `GameLogic`, it has a reason to: `ASSERT_TEXT` is the one
thing it uses from there. Cutting it loose is a one-line change to `NeuronServer.h` plus whatever
`Debug.h` becomes. **The code is portable; the include chain is not.** That distinction
holds for `Lockstep/ShipView.{h,cpp}` and the two mesh headers too — 535 lines that name
nothing platform-specific but are compiled through `Lockstep/pch.h`, which reaches
`<windows.h>` and D3D12 by way of `NeuronClient.h`.

`NeuronClient` contributes 361 portable lines, and one of them matters: `IsometricCamera.{h,cpp}`
is 262 lines of pure arithmetic — the projection, the inverse, the zoom ladder, the pixel snap —
with no graphics API in it. On the second reading of §1 it is also the file most likely to be
thrown away, since a graph-of-systems 4X has no isometric ground plane to un-project onto.

---

## 4. What does not port

### 4.1 The renderer — 2157 lines, and the shape does not survive translation

`NeuronClient` is not a renderer with a D3D12 backend. It is D3D12, expressed directly:
`Device.cpp` builds an adapter, a direct queue, a flip-model swap chain and a per-frame
allocator/fence rotation; `PaletteTarget.cpp` builds committed resources, RTV and DSV heaps,
versioned root signatures and pipeline state objects; `D3D12Defaults.h` exists because
`D3D12_GRAPHICS_PIPELINE_STATE_DESC` has enums with no zero enumerator. None of that is
abstraction that could be re-pointed. Every object has a counterpart on Vulkan and on Metal, and
the counterpart is a different program.

The concepts do map, and mostly cleanly. `DXGI_FORMAT_R8_UINT` as a colour attachment — the whole
premise of ADR-001 — is a mandatory Vulkan format and exists as `MTLPixelFormatR8Uint`, so the
index target survives on both. `Texture2D<uint>.Load()` becomes `texelFetch` and `texture.read()`,
neither of which can filter, so ADR-001's "there is no sampler on this path" property survives too.
Integer-only resolve arithmetic survives. What changes is the resource-state model — explicit
barriers on Vulkan, largely automatic hazard tracking on Metal — and the descriptor model, where
D3D12's single shader-visible heap has no direct equivalent on either.

**One measured incompatibility is worth naming on its own.** `MeshRenderer` passes 36 DWORDs of
root constants: a 4×4 view-projection, a 4×4 world matrix, a light direction and a threshold. That
is **144 bytes**. Vulkan's guaranteed minimum for `maxPushConstantsSize` is **128 bytes**, and an
implementation is conformant reporting exactly that; some Android GPU families do. The mesh pass as
written therefore does not fit in the budget a portable Vulkan backend is entitled to assume, while
the resolve pass (17 DWORDs, 68 bytes) and the starfield (12 DWORDs, 48 bytes) both do. The 128 is
from the specification's required-limits table, not from a device survey — no hardware was
queried for this document. The
fix is small — send the world transform as a heading and a position and rebuild the matrix in the
shader, or move the view-projection to a uniform buffer — but it is a change to the renderer's
contract rather than a change of API call, and it is the sort of thing that is found at the end of
a port rather than the beginning.

A second, smaller one: `TextPS.hlsl` uses `discard`. Mobile GPUs are tile-based, and a fragment
shader that may discard forces late-Z for the whole draw. At 640×400 with an 8×8 status line the
cost is not measurable, but the pattern is one to know about before the UI grows.

### 4.2 The process shell — 374 lines

`Lockstep.cpp` is a Win32 program: `wWinMain`, a registered window class, a `WndProc`, a
`PeekMessage` pump, `SetProcessDpiAwarenessContext`, and the create-then-measure-then-correct dance
that guarantees a client area of exactly 1280×800. On Android the equivalent is an `Activity`, a
`NativeActivity` or `GameActivity` and a `ANativeWindow` reached through JNI, with the message pump
inverted: the OS calls you. On iOS it is a `UIApplicationDelegate`, a `UIViewController` and a
`CAMetalLayer`, with a `CADisplayLink` driving the frame. In both cases the "measure the client
area and correct it" logic has nothing to correct — the surface is whatever size the system says.

The composition root — build the device, start the session, run the loop, catch one exception,
show a message box — becomes lifecycle callbacks (§7). It is the file with the least reusable
content in the tree.

### 4.3 The build system, and the three gates that ride on it

`AGENTS.md` §3 is unambiguous: MSBuild through `Lockstep.slnx`, toolset `v145`,
`/std:c++latest`, `/permissive-`, `/W4` with warnings as errors, `/fp:precise` stated explicitly in
every `.vcxproj`, precompiled headers, `FXCompile` for shaders, and **"there is no CMake."** None
of that reaches Android or iOS. Android builds through Gradle driving CMake or `ndk-build`; iOS
builds through Xcode, and while an Xcode project can be generated, in practice it is CMake there
too.

That is not just a different invocation, because three gates are built on the current one:

`Build/CheckFormat.py` survives — it shells out to `clang-format` and it ran clean on this Linux
container on 2026-09-10 (69 files, 0 unformatted).

`Build/CheckProjectFiles.py` does not. It parses `.vcxproj` and `.slnx` XML to enforce R2, R7, R11,
the flat-directory rule, the shader registration and the Debug/Release alignment that stands in for
the Release build CI does not run. On a CMake tree there are no `.vcxproj` files for it to read. It
is also literally Windows-path-bound: run on Linux it reports all 16 shaders as both missing and
unregistered, because it compares `Shaders\MeshPS.hlsl` against `Shaders/MeshPS.hlsl`. That is a
harmless artefact of running it on the wrong OS, and it is also a fair miniature of the problem —
the checker that enforces the conventions is itself written against one platform.

`Build/RunClangTidy.py` needs `INCLUDE` set by a Developer PowerShell so clang can find the CRT and
the Windows SDK. Against an NDK sysroot it needs a different environment and a different
`HeaderFilterRegex`, but the `.clang-tidy` rules themselves — the naming table, R1, R3, R5, R8 —
are compiler-agnostic and would carry over.

### 4.4 The test suites — 1798 lines

All four are MSVC `CppUnitTest` DLLs run by `vstest.console.exe`. Neither exists on either target.
Three ways out, and each costs something. Keep the Windows suites as the authority and never run
tests on device — defensible for `GameLogic` and `NeuronCore`, which are platform-free, and worth
noticing that it leaves the ported code untested by construction. Adopt a portable framework —
Catch2, doctest, GoogleTest are the obvious three, and R14 forbids all of them. Write a minimal
harness — a few hundred lines, no dependency, and a thing to maintain forever.

`Tests/NeuronClientTests` (896 lines, half the total) is the suite least likely to survive in any
case: much of it tests `PointerInput` against real `WM_POINTERDOWN` messages and a real HWND.

### 4.5 The shader pipeline — free on one target, a rule collision on the other

Eight HLSL files, 255 lines, compiled at build time by `FXCompile` into headers holding
`constexpr` byte arrays, because R13 says nothing loads at runtime and AGENTS.md §2 says
`CompiledShaders/` is build output that is never committed. The *shape* of that pipeline — author
HLSL, compile in the build, embed the result, load nothing — is portable and worth keeping. What
happens inside it splits sharply between the two targets.

**Android is nearly free.** The compiler already in use is dxc, and dxc emits SPIR-V directly with
`-spirv`. The HLSL source survives unchanged as source; what changes is the invocation and the
build step that runs it, which is being rewritten anyway (§4.3). There is no translation layer and
no new dependency: the Vulkan path keeps the same eight files and the same compiler.

**iOS is where R14 actually bites.** Metal consumes MSL, and there is no Microsoft-supplied
HLSL-to-MSL path. Three options, each with a real cost:

*Cross-compile SPIR-V to MSL* with SPIRV-Cross, at build time, so nothing ships but the produced
`.metallib`. It is the standard answer and it is a third-party dependency in the build, which R14
forbids as written. That R14 says "closed list, not a high bar" is the point — this is exactly the
case it was written to force a conversation about.

*Hand-write the MSL.* Eight shaders, 255 lines, none of them complex — the mesh vertex shader is
the only one with real arithmetic in it, and three of the eight are variations on a fullscreen
triangle. It costs no dependency and buys a second copy of every shader to keep in step with the
first, which is precisely the failure mode `Design/README.md` §3.5 warns about: two copies of one
fact is one wrong fact waiting.

*Ship Vulkan everywhere through MoltenVK.* One backend instead of two, and a much larger
third-party dependency than SPIRV-Cross — a translation layer in the shipping binary rather than a
tool in the build.

The honest summary is that a Vulkan-only mobile target costs nothing against R14 and an iOS target
cannot be reached without either amending R14 or accepting duplicated shader sources. That is a
decision, and it belongs to the owner.

---

## 5. What the presentation contract does on a phone

`Design/README.md` §1 fixes 640×400 at sixteen colours, scaled by a whole number. That constraint
is not violated by a phone — it is satisfied badly. Taking the largest integer *n* with
640*n* ≤ width and 400*n* ≤ height, in landscape, against physical panel resolutions:

| Device | Panel | Scale | Presented | Screen used |
|---|---|---:|---|---:|
| iPhone 15 | 2556 × 1179 | 2 | 1280 × 800 | 34% |
| Pixel 8 | 2400 × 1080 | 2 | 1280 × 800 | 40% |
| Galaxy S24 | 2340 × 1080 | 2 | 1280 × 800 | 41% |
| iPad (10th gen) | 2360 × 1640 | 3 | 1920 × 1200 | 60% |
| iPad Pro 12.9″ | 2732 × 2048 | 4 | 2560 × 1600 | 73% |

Arithmetic, not measurement: no device was involved. Phones land on scale 2 and give back roughly
60% of the panel as letterbox, because 640×400 is 1.6∶1 and a modern phone is about 2.2∶1. Tablets
are comfortable. **This is a landscape-tablet contract.**

Portrait is where it stops being a trade-off. On an iPhone 15 held upright the long edge is the one
that has to accommodate 400, so the scale is 1 and the game occupies 640×400 of 1179×2556 — **8.5%
of the screen**. A fixed 640×400 landscape framebuffer cannot be presented in portrait at all, in
any way anyone would ship.

That matters more under the first reading of §1 than the second. The game in the one-pager is a
digest-first, thumb-driven, open-it-after-work game; those are held in portrait. A real-time ship
game held in landscape lives happily inside this constraint on a tablet and awkwardly on a phone.
Either way the decision is the owner's and it is an ADR, because `Design/README.md` §1 makes the
resolution a design constraint rather than a setting.

The rest of the presentation chain is in better shape than the resolution is. Integer scaling with
no sampler survives — both Vulkan and Metal do integer texel loads. The DPI problem
`SetProcessDpiAwarenessContext` solves does not exist on either target in the same form: you are
handed a surface in physical pixels and asked what to do with it. And the retro look itself is
arguably *easier* to defend on a phone, where a pillarboxed 16-colour screen reads as deliberate.

---

## 6. Which recorded rules bind, and how hard

| Rule | On mobile |
|---|---|
| R12 — D3D12 only | Contradicted outright. A new ADR, not a reinterpretation. |
| R13 — the executable ships alone | Survives in spirit, and is easier to honour. An APK and an `.ipa` are archives, but embedded `constexpr` arrays work identically and no working-directory assumption is introduced. |
| R14 — no third-party dependencies | Needs rewording at minimum: "the platform SDK and the standard library" costs nothing. Genuinely bitten by two things — the Metal shader path (§4.5) and the test framework (§4.4). |
| R15 — memory is plain C++ | Survives. |
| R16 — determinism is built | Survives, and §3 argues it is the reason the simulation ports at all. `/fp:precise` has Clang equivalents, and `GameLogic` needs none of them. |
| R17 — string literals are `const` | Survives; Clang is stricter than `/Zc:strictStrings`, not looser. |
| §1 naming, §4 layout | Survive unchanged. `.clang-format` and `.clang-tidy` are compiler-agnostic; `CheckProjectFiles.py` is not (§4.3). |
| §3 x64 only | Contradicted. arm64 on both; Android needs arm64-v8a and, for emulators, x86_64. |
| §3 MSBuild, no CMake | Contradicted (§4.3). |
| Baseline — 640×400, whole-number scale | Survives arithmetically, poorly in practice on a phone (§5). |
| Baseline — one executable, client and server in one process | Survives technically; contradicted by the one-pager's four-ticks-a-day design, which needs a hosted server. |
| Baseline — Windows 11 | Contradicted, definitionally. |

The pattern is worth stating plainly: **nothing about how this code is written stops it moving.
Everything about what it is written against does.** The naming rules, the layout rules, the
determinism rules and the memory rules all travel. The API, the platform, the build system and the
presentation resolution do not.

---

## 7. What the tree has no concept of

These are absences rather than incompatibilities, and they are the part of a port that is
underestimated most reliably.

**Lifecycle.** There is one loop, entered once and left once, and a process that ends when the
window closes. Both targets suspend and resume applications routinely, can terminate a backgrounded
process without warning, and expect state to be saved on the way out. `RunGame` has no seam for any
of it.

**Surface loss is normal, not fatal.** `Device::FailIfDeviceRemoved` treats a lost device as
terminal: read the removal reason, `Fatal`, message box, exit. That is right on Windows, where
device removal means a driver reset or a GPU that went away. On Android the surface is destroyed
and recreated every time the user switches away and back, and `VK_ERROR_OUT_OF_DATE_KHR` on
rotation is ordinary. **The single error path in `Debug.h` — everything becomes one exception
caught at the composition root — is the design that has to change most, and it is 70 lines of
header that a dozen files depend on.**

**Orientation.** No concept of rotation, and per §5 the fixed framebuffer makes one orientation
unusable.

**Safe areas.** The status line is drawn at (8, 8) in virtual texels. On any recent phone that is
somewhere under a notch, a punch-hole or a rounded corner, and there is nothing in the tree that
knows an inset exists.

**The back gesture** on Android, which has no analogue in a program with no keyboard handling and
no navigation model.

**Power and thermals.** `Session::Run` holds a thread at 20 Hz, and `Device::EndFrameAndPresent`
calls `Present(1, 0)` — vsync, so 60 or 120 frames a second, every frame redrawing a full-screen
starfield of per-texel integer hashes. On a desktop that is free. On a phone it is a battery
profile, and it is worth putting next to §1: a game that resolves four times a day has no reason to
run a 20 Hz thread or draw at display rate at all.

**Distribution.** Both targets require signing, store review, a privacy declaration and an age
rating. A three-week-season multiplayer game also acquires an account system, push notifications
(the one-pager's digest is a push notification), and a server someone operates. None of that is
code in this tree, and all of it is on the path to shipping on a phone.

---

## 8. The one part that is already mobile-shaped

`PointerInput` was written touch-first, by decision rather than accident: MVP-01 §2 made touch
primary and the mouse the fallback, `EnableMouseInPointer` collapses both onto one path, and
ADR-008 gives pinch and wheel a single shared output in whole zoom steps so the camera never learns
which the player used. ADR-009 then measured, on hardware, that a wheel notch really does arrive as
`WM_POINTERWHEEL`, and deleted the second code path that had been carried against the possibility
that it did not.

The result is 332 lines of which only the message decoding is Win32. Contact tracking, the
two-contact cap, the cancellation of a tap when a second finger lands, the pinch baseline and the
loop that banks multiple steps out of one large jump — all of that is arithmetic over a list of
contacts, and it is the same arithmetic against Android's `AMotionEvent` pointer indices or iOS's
`UITouch` set. Extracting a platform-free gesture core behind a thin per-platform decoder is the
smallest job in this document and the one with the clearest payoff.

What it does not have: long-press, double-tap, drag-to-pan, momentum, or any notion of a gesture
that competes with the system's own edge swipes. It also has no concept of a tap *target* — a tap
is a point on the ground plane, which is exactly right for the game in the tree and not what a
digest-and-map 4X needs.

---

## 9. The seam that already exists, and it is not the graphics one

The most useful thing this tree has for a second platform is `Neuron::Simulation` (ADR-007) and the
transport behind it.

The server owns an abstract `Simulation` and ticks it; the client cannot move the ship and holds no
rules — `ShipView` accepts states and interpolates, and that is the whole of its opinion (ADR-005).
Between them is `LoopbackTransport`, two fixed-capacity queues with deliberately different overflow
policies (ADR-006), whose header already says what happens next: *"the day a `UdpTransport` exists,
the concept gets named and both implement it. The shape of the interface here is what that concept
will be."*

That means the split a mobile version needs is already drawn, and it is drawn in the right place. A
phone client talking to a hosted server is the same architecture with a different transport
implementation — and since no socket code exists yet (§3), that implementation gets written once,
portable, rather than ported. Under the first reading of §1 this is the *only* part of the
architecture that carries over intact, and it happens to be the part that matters most for a game
of six to twelve humans on a shared clock.

By contrast, the seam a port would want most — a rendering interface with a D3D12 implementation
behind it — does not exist, and R12 is the reason it does not. That was a defensible call for a
single-platform game and it is the reason §4.1 is 2157 lines rather than a backend swap.

---

## 10. What this leaves open

Three decisions, none of them taken here, in the order they have to be taken. Where each stands on
2026-09-12 is noted beneath it.

**Which game.** §1. Until this is settled, every figure below §3 is being applied to a target that
may not exist. It is not a technical question and it is not one this document can answer.

*Settled by the owner on 2026-09-11: the one-pager's game. Mobile is the product and the desktop
client is the prototype (`Design/blueprint.md` §9), so §1's first reading applies — there is no
port, and the question is which parts of the tree a new client keeps.*

**Whether 640×400 is a per-platform constant or a design constant.** `Design/README.md` §1 makes
it a constraint; §5 shows what that constraint does to a phone in landscape and that it forbids
portrait outright. Changing it supersedes part of the baseline and touches ADR-001 and ADR-003,
both of which reason in whole virtual pixels. Keeping it is also a decision, and the cost is
pillarboxing and landscape-only.

*Overtaken: the baseline has been 1280×720 presented 1:1 since ADR-011, and ADR-001 and ADR-003 are
superseded and deprecated. The decision is now whether 1280×720 is a design constant, and
`Design/blueprint.md` §9 owes an ADR revising the baseline when the mobile client starts. §5's
arithmetic has not been redone for the new size.*

**Whether the error model stays.** §7 argues that `Debug.h`'s single path is a desktop assumption
and that surface loss is routine on both targets. This one is worth deciding early even if no port
is ever started, because the answer changes 70 lines of header that everything includes.

*Still open. `Debug.h` still holds the single path; the 2026-09-12 codebase review made `Fatal`
reach its catch in a Release build and changed nothing about the shape.*

Two smaller things do not need a decision, only a note. The push-constant budget in §4.1 is a real
144-versus-128-byte problem whenever a Vulkan backend is written. And R14 has exactly two genuine
collisions — the Metal shader path (§4.5) and the test framework (§4.4) — rather than the general
difficulty it looks like from a distance.
