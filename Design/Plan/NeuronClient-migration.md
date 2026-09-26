# FrontierOutpost on NeuronClient

How FrontierOutpost moves off OpenGL, GLEW, SFML and FreeType onto NeuronClient: Direct3D 12,
DirectWrite, WIC, XAudio2 and Win32.

- **Status:** Proposed, 2026-09-25. The owner took decisions N1–N14 the same day (§2). Phase 0 is
  done (§6): the checkers run in CI, and the startup fixes are in (`ea07b9c`). Phase 1's removals
  are in (`04d440b` to `f20c684`, and `b78a33f` for N14); ADR-013, now Accepted, records what went.
  Its done-when still needs the owner to see the 16 kept apps start. Phase 2's six steps are in
  (from `037add3`): ADR-005, ADR-006 and ADR-010 to ADR-012 are Accepted, NeuronClient has the
  images, glyphs, sound, window and input, and no SFML or FreeType is left. SFML's include
  directories, definition, libraries and `.gitignore` negation went with it, ahead of Phase 5.
  Phase 2's done-when needs the owner to check text, images, sound and input in the kept apps.
  Phase 3 is done (`633b371` to `bbd563f`): ADR-007 and ADR-008 are Accepted, and NeuronClient has
  §5.3's device, resources, context, compute, mips, readbacks, swap chain and present pass, DRED's
  report and PIX's regions, each with its tests. Its done-when holds on `bbd563f`: all 115 tests
  pass on WARP in CI with zero debug-layer errors, and the four builds still link with Phase 2's 522
  warnings, all lt's. ADR-009 stays Proposed until Phase 4, which implements it. The owner took
  N15 and N16 on 2026-09-26. Phase 4 is under way (from `e630300`): step 1 is in, step 2 is in but
  for the four field shaders that step 4 replaces, and so is the first part of the launcher's smoke
  mode (N15). The owner released step 3 on 2026-09-26 (N16); `8bc74a0` is the last commit that
  renders with OpenGL. Step 3 is in (`7a2695e` to `8534b2c`): liblt draws through `DrawContext`, the
  launcher renders offscreen on WARP, and the smoke job runs `loading` and `ui` there for 30 frames
  each, with no debug-layer error, on `8534b2c`. Their frames show step 5.2's interface and text,
  which needs no SDF field, ahead of step 4; presenting (5.1) is the owner's to see, since the smoke
  run makes no swap chain. The four builds link with 507 warnings, 458 unique, all lt's. Until
  step 4, an app that draws an SDF field ends with a fatal error that says so.
- **Scope:** `FrontierOutpost.slnx`, `FrontierOutpost/`, `GameData/`, and two new projects at the
  repository root: `NeuronClient/` and `Tests/NeuronClientTests/`.
- **Paths:** relative to the repository root. `liblt/` is short for `FrontierOutpost/src/liblt/`, and
  engine paths without a prefix (`LTE/…`, `Game/…`, `UI/…`, `Module/…` and so on) are in it.
- **Inputs:** a survey of this tree on 2026-09-25 (renderer, shaders and scripts, platform code, dead
  code), and `Zwaliebaba/Outpost.Commander` at `15f9949` for NeuronClient's conventions.

## 1. Summary

**Today** liblt renders through OpenGL 2.1 by way of GLEW 1.7. It gets its window, input,
threads, clocks, image codecs and one HTTP client from SFML 2.5. It rasterises glyphs with FreeType
2.5.5, and already plays sound through XAudio2 (ADR-003).

**The change** puts graphics, glyphs, images, sound, the window and input behind **NeuronClient**,
a new static library linked into `lt.dll`:
- Direct3D 12 for graphics;
- DirectWrite for glyphs;
- WIC for images;
- XAudio2 and X3DAudio for sound;
- plain Win32 for the window and input.

Threads and clocks move to the C++ standard library. liblt keeps its own calls for OS services:
paths and folders, message boxes, crash dumps and the entry point (N12).

**liblt keeps its API** (`Renderer_*`, `Texture2D`, `Shader`, `ShaderInstance`, `Mesh`, `Font`,
`Window`, `Keyboard`, `Mouse`, `SoundEngine`), so scripts change only where a feature is removed.
Under that API, the OpenGL layer is rewritten onto NeuronClient, and the GLSL in `GameData/shader`
becomes HLSL compiled into `lt.dll` at build time.

**Removal comes first, while OpenGL still renders:** 12 toy apps, unreachable engine code, eight dead
render passes, 38 shader files, 34 font families, and the sounds and data files nothing names.

**At the end** the solution holds `lt`, `launch`, `NeuronClient` and `NeuronClientTests`. The tree
has no SFML, GLEW, FreeType, OpenGL or `GameData/shader`.

## 2. Decisions

| # | Decision | By |
|---|---|---|
| N0 | **NeuronClient is new, written for this engine.** The NeuronClient in Outpost.Commander is a reference for conventions, not code to copy: it is UWP and CoreWindow, touch only, has no audio and loads only system fonts. Graphics are Direct3D 12, sound XAudio2, and the platform Win32. DirectWrite replaces FreeType, and unused features are removed. | owner (brief) |
| N1 | **ARM64 stays.** NeuronClient builds for x64 and ARM64, under an ADR that amends AGENTS.md §3 for it. | owner |
| N2 | **Shaders are compiled at build time into headers.** | owner |
| N3 | **Free to modernise.** Parity with the OpenGL build is not the acceptance bar, and there are no reference captures. | owner |
| N4 | **Remove all four tiers of unused material:** toy and test apps, unreachable code, dead passes and shaders, unused assets (§9). | owner |
| N5 | ~~**AVX2 on x64.** NeuronClient compiles with `/arch:AVX2`, which puts the game's CPU floor at Intel Haswell or AMD Excavator.~~ **Replaced on x64 by N10.** ARM64 keeps the compiler's default. | owner |
| N6 | **A missing sound is a logged warning,** not the end of the program. The owner's WAV conversion (O10) can land whenever it is ready. | owner |
| N7 | **Vsync stays off,** as it is today. | owner |
| N8 | **RandomScreenshot shows a plain colour,** in place of a screenshot from the original author's folder. | owner |
| N9 | **A sound file that exists but that XAudio2 cannot play stays fatal.** N6 covers missing files only: an unplayable one is a broken asset. | owner |
| N10 | **No AVX2.** NeuronClient states the instruction sets `lt` states: SSE2 on x64, and the compiler's default on ARM64. Inside `lt.dll`, a template or inline function that both libraries use keeps one copy, which the linker picks, so an AVX2 NeuronClient could run AVX2 code in liblt, even before a CPU check. There is no CPU floor beyond x64's and no check. | owner |
| N11 | **liblt's shader folder is new code.** `FrontierOutpost/src/liblt/Shaders/` is carved out of ADR-001's exemption, so AGENTS.md's shader rules and the checkers, the registry check among them, apply to it. The rest of `lt` stays exempt. | owner |
| N12 | **liblt keeps its own calls for OS services:** `SHGetFolderPath`, `GetModuleFileNameA`, `CreateDirectoryA`, `MessageBoxA` and DbgHelp in `LTE/OS.cpp`, `MessageBoxA` in `Common.cpp`'s assertion handler, and `WinMain` in `LTE/LTE.h`. NeuronClient takes what OpenGL, GLEW, SFML and FreeType did, and XAudio2. | owner |
| N13 | **No `gl-final` tag and no frame-time comparison.** The OpenGL build stays in the history, but nothing marks it and nothing is measured against it. The owner judges each phase by the port itself. | owner |
| N14 | **The script API is an interface, not unused material.** Of the 265 natives that neither a script nor C++ reached after Phase 1, the 12 that were the only way into code of their own went, with what only they reached. The other 253 stay: 194 are members of families one macro builds per object type, item field, component or key, and 59 are short, none over 11 lines. | owner |
| N15 | **The smoke job comes before bring-up** (2026-09-26). Step 7's CI job lands with step 3, and the launcher's smoke mode before it: `--frames` and `--capture` before the pause, `--warp` and offscreen rendering with step 3, which gives liblt a device to put on WARP. Besides failing on a Direct3D 12 error, the job writes a downscaled copy of each app's frame into its log, since the session that ports liblt cannot fetch CI artefacts. Each bring-up step is checked in those frames, and the owner checks on a GPU at the done-when. | owner |
| N16 | **Work pauses before Phase 4 step 3** (2026-09-26) until the owner has checked Phases 1 and 2 in the apps while OpenGL still renders. Steps 1 and 2 and the smoke mode's first part change nothing OpenGL is given. The owner released step 3 the same day; `8bc74a0` is the last commit that renders with OpenGL. | owner |

What N2 and N3 mean in practice. The first two points correct what the question offered:

- **N2 uses FXC, not DXC.** liblt binds uniforms and textures by name: 243 lines in 46 C++ files,
  and 57 in scripts. That needs shader reflection at run time.
  - DXIL, DXC's output, can only be reflected by `dxcompiler.dll`, a redistributable (R14).
  - DXBC, FXC's output, is reflected by `D3DReflect` in `d3dcompiler_47.dll`, which ships with
    Windows 10 and 11.
  - So shaders build with `FxCompile` at Shader Model 5.1, reflection runs at load, and nothing is
    compiled at run time.
  - SM 5.1 also keeps Windows 10. Outpost.Commander's SM 6.7 raised its floor to Windows 11 22H2
    (its ADR-012).
- **N2 keeps the script filters.** Their 19 `Shader_Create` calls (`GameData/script/Texture/Filters.lts`)
  name literal files, so a registry keyed by the legacy name covers them. The observatory and image
  apps keep working.
- **N2 removes hot reload:** `Shader_RecompileAll`, and the T key that calls it in 7 apps.
- **N2 replaces generated shader code.** `SDFMesh` compiles one program per mesh from
  `#define FIELDFN <GLSL>`, built from a randomly seeded SDF tree (`liblt/LTE/SDFMesh.cpp:199-200`).
  That cannot be enumerated at build time. §5.7 replaces it with a precompiled compute shader that
  interprets the tree.
- **N3 lets the port take the cheaper Direct3D 12 route** wherever emulating OpenGL would cost more,
  and nothing beyond that. §5.3 and §5.5 list those choices. Visual modernisation (sRGB, HDR, MSAA)
  is backlog (§11), not part of the port.

## 3. Where the brief is weakest

1. **Direct3D 12 buys this renderer nothing it needs, and costs the most code.** liblt is a 2013
   OpenGL design:
   - render state is implicit;
   - uniforms are set by name and persist per program;
   - textures bind to units when the uniform is set;
   - `glGenerateMipmap` runs on every texture it creates;
   - readbacks are synchronous, lens-flare visibility among them every frame;
   - `glFinish` runs at every present (`liblt/LTE/Renderer.cpp:608-610`);
   - resources are created and destroyed mid-frame.

   Direct3D 12 provides none of that. NeuronClient must first build a D3D11-style layer: state
   tracking and barriers, descriptor rings, a pipeline-state cache, upload and readback rings,
   release deferred until the GPU is done, and a compute mip generator. That layer is the largest
   single piece of new code, and Direct3D 11 would have supplied it. Direct3D 12 holds up only as
   the shared base of the owner's NeuronClient projects. ADR-007 records that reason so it is not
   reopened.
2. **Build-time shaders cost two runtime features and pin a legacy compiler.** Hot reload goes, and
   SDF fields need a redesign (§5.7). FXC is frozen at SM 5.1. Nothing in this content needs more,
   but a later move to DXC must move reflection to build time as well.
3. **Nothing automatic can say the port looks right.** Under N3, CI can prove three things: the
   game builds, NeuronClient's tests pass, and each 3D app renders frames on WARP without a
   Direct3D 12 error. It cannot prove that the frames look right, that sound plays, or that ARM64
   works. Those rest on the owner's GPU, speakers and ARM64 device, phase by phase. There is no
   tagged OpenGL build to compare against (N13).
4. **ARM64 cannot be verified in CI.** No runner executes ARM64, and ARM64 GPU drivers see the
   least Direct3D 12 use. Every ARM64 claim waits for the owner's device.
5. **This is a sixth NeuronClient.** Five repositories already carry copies that have drifted
   apart, from 45 to 139 files. This one is written game-agnostic (R9), so it could become the
   shared one. This plan does not extract it.
6. **Most of the apps to keep could not start.** Phase 0 fixed all three causes (`ea07b9c`):
   - `GameData/script/Texture/RandomScreenshot.lts:2` loaded `/home/josh/Dropbox/lt/screenshot`,
     the original author's folder. Seven kept apps and the DevPanel use it, and a failed load exits
     the program. N8 replaced it with a plain colour.
   - The kept `image` app loaded two files that exist nowhere, and so exited before RandomScreenshot
     was reached:
     - `data/screenshot/29.png`, the branch its `?` always took (`GameData/script/App/image.lts:11`);
     - `data/screenshot/10.png`, loaded into a variable that nothing read
       (`GameData/script/Widget/ImageEditor.lts:23`).
   - 9 of the 26 sounds the scripts play have no WAV yet (O10). Each stopped the game the first time
     it played; N6 turned that into a logged warning.

   Nothing could be checked at run time until all three were fixed.

## 4. Starting point

### 4.1 Dependencies and where they are used

| Dependency | What liblt uses it for | Where | Replaced by |
|---|---|---|---|
| SFML 2.5.0: 6 projects, 4 linked | Window, GL context and events; keyboard and mouse polling; joystick; threads, mutex, sleep and clock; image load and save; one HTTP client | 10 `.cpp` files, no header | NeuronClient for window, input and images; the standard library for threads and clocks; joystick and HTTP removed |
| GLEW 1.7.0 and OpenGL 2.1 (compatibility profile) | All rendering | `LTE/GL.h`; gl calls in 9 files; GL types in 6 public headers; about 30 call sites pass `GL_TextureFormat` | NeuronClient (Direct3D 12) |
| FreeType 2.5.5 | 64 px glyph coverage for the text atlas; kerning from the `kern` table | `LTE/Font.cpp` only, 11 functions | NeuronClient (DirectWrite) |
| XAudio2 and X3DAudio | All sound (ADR-003) | `Module/SoundEngine/XAudio2.cpp`, 743 lines | NeuronClient; liblt keeps an adapter |

### 4.2 The renderer

- **Draws.** 66 GL entry points are live, and there is one draw call: `glDrawElements` with
  triangles. It is fed four ways:
  - meshes in VBOs and IBOs;
  - a quad from client memory, with 8-bit indices;
  - client-side vertex arrays;
  - vertex structs described by liblt's type reflection.

  There are no lines, points, stencil, polygon offset or instancing. Wireframe exists, but nothing
  enables it.
- **State.** Blend off, alpha or additive; depth test (`LESS`) and depth write; cull back or off;
  scissor; viewport. All of it is pushed and popped on stacks.
- **Render targets.**
  - Four colour-slot stacks and a depth stack, resolved into cached FBOs. At most two targets are
    written at once.
  - Formats: R8, RG8, RGBA8, R16F, RGBA16F, R32F, RGBA32F, D32F.
  - Cube faces are rendered into. 3D textures are filled by copying 2D slices through the CPU.
  - The full-resolution depth buffer stays bound under the quarter-resolution bloom targets.
- **Synchronisation.** `glFinish` after every present, plus synchronous readbacks:
  - lens-flare visibility, every frame (`Game/RenderPass/LensFlares.cpp:159-188`);
  - IR-map generation;
  - SDF slices;
  - plate-mesh occlusion;
  - disk-cache saves of cube maps, 96 MiB each;
  - screenshots.
- **Mipmaps.** `glGenerateMipmap` runs when any 2D texture is created, render targets included
  (`LTE/Texture2D.cpp:128`). It also runs after each new glyph and after cube generation.
- **Uniforms.**
  - Set by name; values persist per program across passes and frames.
  - Setting one makes its program current, and some draws rely on that
    (`LTE/RenderPass/Bloom.cpp:40-63`).
  - Textures bind to units round-robin when their uniform is set.
  - The location cache is keyed by the name's pointer, not its text (`LTE/Shader.cpp:31, 248-264`).
- **Shaders.**
  - 169 `.jsl` files (GLSL 1.20 with `EXT_gpu_shader4`), 6,357 lines.
  - The engine prepends `#version 120` and runs its own preprocessor for `#include` and `#output`
    (`LTE/Shader.cpp:23, 47-91`).
  - 104 vertex/fragment pairs are reachable, plus one generated program per SDF mesh.
- **Conventions.**
  - The GL projection puts z in [−1, 1].
  - Most vertex shaders overwrite `gl_Position.z` with a logarithmic depth
    (`GameData/shader/common/vert.jsl:29-32`).
  - Window-space viewports are flipped against the window height (`LTE/Viewport.cpp:59-66`).
  - Shaders use the quad's uv and `gl_FragCoord * rcpFrame` interchangeably.
  - The present samples with v flipped (`UI/Widget/Rendered.cpp:142-154`).
- **A latent defect.** Nothing calls `glPixelStore`, so SMAA's search texture, 66 bytes wide, is
  uploaded with 4-byte row alignment and comes out sheared (`ThirdParty/SMAA_SearchTex.h:4-7`). The
  port uploads it correctly, so SMAA's output will change.

### 4.3 Text, input, window, sound

- **Text.**
  - FreeType renders each glyph at 64 px into an R16F coverage atlas.
  - A GPU pass (`GameData/shader/fragment/compute/sdffont.jsl`, 129² fetches per texel) turns it into
    an R32F distance field, whose mipmaps are regenerated after every glyph.
  - Text then draws at any size from the distance field (`LTE/Font.cpp:74-190`).
  - Four families are used: Rajdhani, Iceland, SourceCodePro and Gafata. Play is used only by the
    `font` toy app.
- **Input.** The engine's `Key` enumeration (101 keys) is its own. SFML's codes are converted on
  arrival; they are never stored, persisted or shown to scripts, which name keys (`Key_T`). So a
  Win32 mapping has no compatibility constraint.
- **Window.**
  - A 1920×1080 client area, windowed, with vsync off and no frame cap.
  - The OS cursor is hidden; scripts draw their own.
  - The process is system-DPI-aware (SFML's choice).
  - SFML swallows `WM_CLOSE`, so the close button and Alt+F4 do nothing today.
- **Sound.** Already XAudio2. What moves is where the code lives, not what it does.

## 5. Target architecture

### 5.1 Layers

```
launch.exe         console program; main()                            legacy (ADR-001)
    │
lt.dll  (liblt)    engine and LTSL; owns every game concept           legacy (ADR-001)
    │  links statically
NeuronClient.lib   graphics, glyphs, images, sound, window, input     AGENTS.md in full, plus ARM64 (N1)
    │
Windows SDK        d3d12 dxgi d3dcompiler dwrite windowscodecs xaudio2 user32 ...
```

- **NeuronClient knows nothing of the game (R9).** No `String`, `V3`, `Object` or `Key_*`: only C++
  and its own small types.
- **No public header includes a Windows, DXGI or Direct3D header.** Each class keeps its platform
  objects in a private implementation, as Outpost.Commander's NeuronClient does. The reason here is
  concrete: liblt defines `near`, `far`, `DrawState`, `interface` and `GetObject`, which are all
  Windows macros, and its include order is load-bearing (MIGRATION_NOTES.md H3).
- **It is a static library.** Of the game's binaries, only `lt.dll` links it, and `launch.exe`
  never sees it; NeuronClientTests links it too. It uses `/MD` and `/MDd`, and `lt`'s `/arch`, to
  match `lt` (N10).
- **liblt keeps its own calls for OS services** (N12): paths and folders, message boxes, crash dumps
  and `WinMain`.
- **Conventions.**
  - Namespace `Neuron`. Unicode internally, UTF-8 at the API.
  - COM objects in `Microsoft::WRL::ComPtr` (R12).
  - No NuGet package, and no `d3dx12.h`, which is not in the SDK (R14).

### 5.2 Components

| Component | Responsibility | Windows API | Replaces |
|---|---|---|---|
| `Window` | An HWND with a given client size and title; the message pump; focus, close and resize; cursor visibility; mouse capture while a button is held; an input-event queue (key down and up with repeat, characters, mouse buttons, moves and wheel) | user32 | SFML's window and events; keyboard and mouse polling |
| `Key` | NeuronClient's key enumeration and its mapping from virtual keys and scan codes, left and right Shift, Ctrl and Alt included | — | SFML's key tables |
| `GraphicsDevice` | Adapter (the default, or WARP on request), device, queue, fences, frames in flight, release deferred by fence, a DRED report on device removal | d3d12, dxgi | The GL context |
| `SwapChain` | A flip-discard swap chain on the HWND; resize; vsync or tearing | dxgi | `SwapBuffers` |
| `Texture`, `Buffer` | 2D, cube and 3D textures in the eight formats, with mip chains; static and per-frame buffers | d3d12 | GL textures, VBOs, IBOs |
| `Program` | Vertex and pixel, or compute, bytecode plus its reflection: constants, textures and samplers by name, and the input signature | d3d12, `D3DReflect` | GL programs |
| `DrawContext` | A D3D11-style immediate context: target and depth binding, viewport, scissor, blend, depth and raster state, program, constants, textures and samplers, static or transient geometry, draw, clear, copy, mip generation, synchronous and asynchronous readback. Inside it: a PSO cache, automatic barriers and descriptor rings | d3d12 | OpenGL's state machine |
| `FontFace` | A DirectWrite face opened from a file; glyph lookup; 8-bit coverage at a pixel size; advance and bearing; pair adjustment from the `kern` table | dwrite | FreeType |
| `ImageFile` | Decode PNG and JPEG to RGBA8, top row first; encode PNG | windowscodecs | `sf::Image` |
| `AudioDevice`, `SoundBuffer`, `Voice`, `Listener`, `Emitter` | The XAudio2 mastering voice and voice pool; WAV parsing (PCM, float, MS-ADPCM); X3DAudio positioning | xaudio2 | The mechanism in `XAudio2.cpp` |

Not in NeuronClient:
- threads, mutexes and clocks, which liblt takes from the standard library directly;
- networking, since nothing is left that uses it;
- joysticks, which are removed.

### 5.3 The graphics core

- **Floor.** Feature level 11_0, Shader Model 5.1. That covers every Direct3D 12 GPU and WARP, and
  Windows 10 stays supported.
- **Frames.** Two frames in flight on one direct queue. The GL build's per-frame wait for the GPU
  goes (N3).
- **Root signatures.** Both are built in C++:
  - one for every liblt program: a root CBV at `b0` (the program's `$Globals`), an SRV table
    `t0`–`t15`, and a sampler table `s0`–`s15`;
  - one for compute: a CBV, an SRV table and a UAV table.
- **Descriptors.**
  - Views are created in CPU-only heaps.
  - A shader-visible CBV/SRV/UAV ring per frame.
  - One shader-visible sampler heap, 2,048 at most, whose tables are deduplicated by content.
    Static samplers would not do, because the SDF field's border colour is red (1, 0, 0, 0).
- **Pipeline states.** Cached under a key of: program, blend, depth, cull, render-target formats and
  count, depth format, input layout and topology. They are created at first use. An
  `ID3D12PipelineLibrary` disk cache is backlog.
- **Barriers.** State is tracked per subresource, and the context inserts the transitions.
- **Lifetimes.** Every release waits in a queue until its fence retires, because liblt creates and
  destroys textures mid-frame: on resize, in generators, for SDF slices and for glyphs.
- **Uploads.** A per-frame ring for constants, transient geometry and small texture updates. Large
  uploads get their own staging buffer.
- **Readbacks.**
  - Synchronous (submit, then wait) for load-time work and screenshots.
  - Asynchronous for per-frame work: the lens flares read the newest completed result, one or two
    frames old (N3).
- **Mipmaps.** A compute downsampler (2×2 box, odd sizes handled) for 2D and cube textures in all
  seven colour formats. It runs when liblt asks for mips on a texture it samples with mips, not on
  every creation (N3).
- **Resource flags.** Colour textures allow render-target use, because liblt may bind any texture as
  a target. Those with mips also allow unordered access. Depth is `D32_FLOAT`.
- **Presentation.**
  - Everything renders offscreen, and a present pass copies to the back buffer. It does the one
    vertical flip of §5.5.
  - Content renders at the client area's size, one pixel to one pixel, as it does today: the
    passes size their targets from the frame and recreate them when it changes size. Nothing is
    authored at a fixed resolution and scaled.
  - Two flip-discard buffers.
  - Vsync off, with tearing where supported, as the original asks
    (`FrontierOutpost/src/launch/launch.cpp:41`; N7).
  - Resize through `ResizeBuffers`.
  - No exclusive fullscreen: nothing uses it.
- **Device removal.** DRED breadcrumbs and page-fault data go to the log, and the engine exits
  through its fatal path. There is no recovery: liblt generates too much GPU content at run time to
  rebuild it.
- **Debugging.**
  - The Direct3D 12 debug layer runs in Debug, with GPU-based validation behind a switch.
  - PIX markers are written without WinPixEventRuntime (R14).

### 5.4 liblt on NeuronClient

| liblt | Change |
|---|---|
| `LTE/GL.h`, `GLEnum.h`, `GLType.h` | Deleted. `Renderer.h`, `Texture2D.h`, `Texture3D.h`, `CubeMap.h`, `Mesh.h` and `Shader.h` take engine-owned enumerations; about 30 call sites change spelling only. |
| `LTE/Renderer.cpp` | The state and render-target stacks feed a `DrawContext`, and the FBO cache goes. Meshes become static buffers; the other three paths draw transient geometry from the ring. The quad's 8-bit indices become 16-bit. `Renderer_Flush` stops waiting for the GPU. |
| `LTE/Texture2D.cpp`, `Texture3D.cpp`, `CubeMap.cpp` | NeuronClient textures. `SetData` converts on the CPU where GL converted during upload: float to half, float to unorm, RGB to RGBA. `GetData` and `SaveTo` read back. Images load and save through `ImageFile`. |
| `LTE/Mesh.cpp` | NeuronClient buffers. A version bump uploads a new buffer, and the old one is released once its fence retires. |
| `LTE/Shader.cpp`, `ShaderInstance.cpp` | Programs come from the compiled registry, by their legacy name. Each program keeps a CPU copy of its constants, so values persist as they did in GL. Names are resolved through reflection and cached by their text. Setting a value makes the program current, as `Use()` did. `JSLPreprocess` and `Shader_RecompileAll` go. |
| `LTE/SDFMesh.cpp`, `LTE/SDF*.cpp` | `GetCode` and `FIELDFN` give way to an instruction encoder and one compute dispatch (§5.7). |
| `LTE/Font.cpp` | `FontFace` replaces FreeType. The distance-field pass stays on the GPU. |
| `LTE/Window.cpp`, `Keyboard.cpp`, `Mouse.cpp` | Events come from `Neuron::Window`, through a table from `Neuron::Key` to the engine's `Key`. Joystick polling goes. |
| `LTE/Thread.cpp`, `Lock.cpp`, `Timer.cpp` | `std::jthread`, `std::this_thread::sleep_for`, `std::recursive_mutex` (SFML's mutex is recursive) and `std::chrono::steady_clock`. The `Timer` class keeps its interface, so its callers do not change. `TerminateThread` becomes a cooperative stop, or a detach at exit. |
| `Module/SoundEngine/XAudio2.cpp` | An adapter over `Neuron::AudioDevice`. Carriers, the camera as listener and the `distanceDiv` mapping stay in liblt. |
| `UI/Widget/Rendered.cpp`, `Module/Scheduler.cpp` | Their raw GL calls go: a depth texture and `glFinish`. |
| `FrontierOutpost/src/launch/launch.cpp` | Gains a smoke mode for CI: `--warp`, `--frames N`, `--capture <path>`. |

### 5.5 Keeping OpenGL's conventions without OpenGL

The port keeps OpenGL's memory layout, with row 0 at the bottom of an image. The alternative is to
convert 130 shader files, and the C++ that places viewports and scissors and flips images, to
Direct3D's top-left origin. Instead:

- **Clip space.** Every vertex shader ends in one macro. It negates clip-space y, and maps z from
  GL's [−w, w] to Direct3D's [0, w] as z′ = (z + w) / 2. Stored depth and clipping then match GL's,
  including the logarithmic depth and the skybox's z = w(1 − 10⁻⁶).
- **Winding.** Negating y reverses the winding, so the rasteriser's front face flips with it.
- **Viewport and scissor.** The rectangles keep liblt's numbers. Once images are stored bottom-up,
  GL's bottom-left y and Direct3D's top-left y name the same row.
- **`gl_FragCoord`.** It becomes `SV_Position` with the same meaning, so uv and
  `gl_FragCoord * rcpFrame` stay interchangeable, as the shaders assume.
- **Flips.** The present pass flips once, into the back buffer. Readbacks keep liblt's existing CPU
  flips.

N3 would allow converting to Direct3D's own layout instead. It is not done here because a wrong flip
is the likeliest porting bug, and the hardest to see across 130 files. This way every convention
lives in one macro and one pass. Converting later, pass by pass, stays possible.

Other rules the context applies:

| GL behaviour | Direct3D 12 rule |
|---|---|
| Formats | R8, RG8 and RGBA8 → `R8_UNORM`, `R8G8_UNORM`, `R8G8B8A8_UNORM`. R16F and RGBA16F → `R16_FLOAT`, `R16G16B16A16_FLOAT`. R32F and RGBA32F → `R32_FLOAT`, `R32G32B32A32_FLOAT`. D32F → `D32_FLOAT`. No sRGB: the game does its own gamma. |
| Filtering and wrapping are properties of the texture | A sampler is chosen at bind time from the texture's settings. Anisotropy only with mipmapped linear minification. The border colour is kept. |
| A depth buffer stays attached under targets of another size | Depth is bound only when the draw tests or writes it, since Direct3D 12 requires matching sizes. |
| Slot 1 stays attached under a job that writes only slot 0 | Only as many targets are bound as the program writes. |
| A program writes to a draw buffer with nothing attached, and the write is discarded | The pipeline state names only the targets set, and what a program writes past them is discarded. Depth alone can be drawn into. |
| An attribute array that is not enabled reads the current attribute, which liblt leaves at (0, 0, 0, 1) | An input the layout has no attribute for reads (0, 0, 0, 1), from a buffer holding one instance. Channels an attribute lacks read 0, 0 and 1, as in GL. |
| `glTexSubImage` replaces part of a level | Updates take a region of a mip: a rectangle, a cube face's, or a box of a 3D texture. |
| `glClear` clears only inside the scissor | Clears take a rectangle, clipped to the level. |
| Blend modes | Alpha = (SRC_ALPHA, INV_SRC_ALPHA, ONE, ONE). Additive = (ONE, ONE, ONE, ONE). One state for all targets. |

### 5.6 Shaders

- **Where they live.** `FrontierOutpost/src/liblt/Shaders/`, flat, as `*.hlsl` and `*.hlsli`. The
  folder is outside ADR-001's exemption (N11), so AGENTS.md's shader rules and the checkers apply.
  - Files are named from the legacy path: `post/blur.jsl` becomes `PostBlurPS.hlsl` (AGENTS.md §2;
    ADR-008 fixes the rule).
  - Output goes to `FrontierOutpost/src/liblt/CompiledShaders/`, which is build output and
    git-ignored.
  - NeuronClient's own shaders (mip generation, present) live in `NeuronClient/Shaders/` and compile
    the same way.
- **How they compile.** One `FxCompile` item per stage in the owning `.vcxproj`, at Shader Model
  5.1, the same in Debug and Release, into a header holding a byte array.
- **Registry.**
  - A table maps the legacy names (`identity.jsl`, `post/blur.jsl`) to compiled programs.
    `Shader_Create(vs, fs)` looks both up; an unknown name is fatal and names the path.
  - The project checker verifies that every `Shaders/*.hlsl` is registered, and that every
    registered name has a file.
- **Names.** HLSL reserves `texture` and `sample`, and has `saturate` and `noise` as intrinsics. The
  HLSL renames them, and the reflection layer maps the GLSL names, so C++ and scripts keep writing
  `"texture"`.
- **Includes.** `global.jsl`, `vert.jsl` and `frag.jsl` become one `Common.hlsli`. It holds the §5.5
  macro, the output struct that replaces `#output`, and the HIGHQ/LOWQ switch.
- **SMAA.** `GameData/shader/common/smaa.jsl` is SMAA.h itself. Only its short porting block is
  hard-wired to GLSL (`smaa.jsl:373-385`), and the algorithm is already written in SMAA's HLSL-typed
  dialect.
  - The port puts back SMAA's own HLSL 4.1 porting block, so no third-party code is added.
  - SMAA's licence notice goes beside the file, which closes O14.
- **How much.** After Phase 1, 130 files remain, 5,311 lines. Setting SMAA.h aside, about 4,300
  lines of GLSL are ported by hand.
- **What goes:** `JSLPreprocess`, the `#version` injection, hot reload, and `GameData/shader`.

### 5.7 SDF fields without generated code

Every SDF node already has a CPU `Evaluate`. But the two noise nodes are `NOT_IMPLEMENTED` there
(`LTE/SDF.cpp`), and fields reach about 256³ voxels (`LTE/SDFMesh.cpp:27`). Evaluating on the CPU
would be slow, and it would first need the noise ported to C++.

So the field moves to a precompiled compute shader:
- **The tree becomes an instruction stream:** postfix, opcode and parameters, in a structured
  buffer.
- **One compute shader walks it for each voxel,** with a value stack and a point stack, and writes
  the R32F 3D texture directly through a UAV. The slice-by-slice copy through the CPU goes.
- **Gradient and occlusion** become compute passes.
- **The LOD grids** are read back for the CPU polygoniser, asynchronously.
- **The opcodes** are the SDF node types Phase 1 leaves. Of the two noise nodes, only
  `FractalWorley` is constructed (`Game/Renderable/Asteroid.cpp:18`), so Worley noise is ported to
  HLSL once. `FractalPerlin`, which nothing constructs, goes in Phase 1 (§9 C).

The interpreter is slower than code specialised per mesh. It runs at generation time, and Phase 4
measures it.

## 6. Phases

**The rule:** one change at a time, and all four builds (x64 and ARM64, Debug and Release) pass after
every step. CI builds Debug|x64. The other three are built by a temporary workflow for the length of
the migration, as the last migration did, or by hand.

Phases 1 and 2 happen while OpenGL still renders, so a fault there cannot be the new renderer's.
Phase 3 touches nothing in liblt; it can start once step 1 of Phase 2 has landed.

### Phase 0: Preconditions

1. **ADRs.** Write the ADRs of §8 as Proposed, for the owner to accept.
2. **Checkers.** Write the checkers AGENTS.md §6 relies on, because NeuronClient is the first code
   they will gate:
   - `Build/CheckFormat.py`;
   - `Build/CheckProjectFiles.py`, allowing ARM64 for NeuronClient (N1);
   - `Build/RunClangTidy.py`.

   Outpost.Commander's `Scripts/` already checks x64 and ARM64 pairs and can be adapted. All three
   checkers leave `FrontierOutpost/` and `GameData/` alone (ADR-001, ADR-004). Two fixes go with
   them:
   - `.clang-tidy`'s `HeaderFilterRegex` names `FrontierCommander`, a leftover from another tree. As
     it stands, it would check no NeuronClient header.
   - The clang-format pin: `.clang-format` says 18.1.3, but `build.yml` installs 22.1.3.
3. ~~**Tag.** Tag the last OpenGL build `gl-final`.~~ Dropped (N13).
4. **Make the apps to keep start.**
   - **RandomScreenshot returns a plain colour (N8).** `Get` builds a 1×1 texture with calls
     scripts already have (`Texture2D_Create`, `BeginDrawTo`, `DrawClear`, `EndDrawTo`) and reads no
     file.
     - The colour is one constant in `RandomScreenshot.lts`. It starts as a dark grey, and the owner
       can change it.
     - The name stays, so its nine callers do not change: seven kept apps, `launcher` and the
       DevPanel.
     - The branch through `Texture/Filters:Artistic` goes with the folder. Its `switch` never took
       it, and Phase 1's reachability decides whether that filter is still used.
   - **The `image` app's two missing files go** (§3). Its `?` keeps only the RandomScreenshot
     branch, so it opens N8's colour, and the editor's unread load of `10.png` is deleted.
   - A missing sound becomes a logged warning, naming each file once, and the sound plays silence
     (N6). Today a missing file ends the program through `Log_Critical`
     (`Module/SoundEngine/XAudio2.cpp:548-561`). A file that is present but that XAudio2 cannot play
     still stops at the assertion handler, as ADR-003 has it and N9 confirms: that is a broken
     asset, not a missing one.

**Done when:** the checkers run green in CI, and the 16 apps to keep start on the owner's GPU.

### Phase 1: Remove what is unused

1. **Apps first.** Then re-run the reachability analysis, over C++ callers and script callers,
   because removing the apps shrinks what everything else reaches. The analysis also covers every
   sound that code or scripts name:
   - it lists each one that `GameData/sound` has no WAV for. That list, not the log, is how N6's
     warnings stay visible;
   - it parses each WAV that is there by the engine's own rules (PCM, IEEE float or MS-ADPCM), so a
     file that would stop the game (N9) is found before the game plays it. A converted file in a
     format XAudio2 refuses, such as IMA ADPCM, is the likely case (O10).

   `Build/CheckSounds.py` does both for sounds, and CI runs it on every change. It fails only on a
   WAV file that cannot be played.
2. **Then the other tiers,** one commit each: code, then passes and shaders, then assets. The lists
   are in §9.
3. **ADR-013** records what went.

**Done when:** all four builds pass, the kept apps start, and nothing refers to a removed name.

### Phase 2: NeuronClient, and every swap that is not graphics

OpenGL still renders throughout. Each step is its own commit.

1. **The projects.**
   - `NeuronClient/` (a static library) and `Tests/NeuronClientTests/`, both in the solution. The
     tests keep the placeholder `SuiteSmoke` until real tests land.
   - The tests build into `x64\Debug\`, where `build.yml` already looks.
   - The first test creates a WARP device, clears a texture and reads it back. That proves the
     runner can run Direct3D 12.
   - A second test loads `d3dcompiler_47.dll` and reflects a compiled blob. The owner runs it on the
     ARM64 device as well.
   - Both state `lt`'s instruction sets, `/arch:SSE2` on x64 and the compiler's default on ARM64
     (N5, N10). The project checker enforces it.
2. **Threads and clocks.** liblt moves to the standard library. No NeuronClient code is involved.
3. **Images.**
   - `ImageFile` (WIC) replaces `sf::Image`.
   - `Window.cpp` and `Mouse.cpp` move from `sf::RenderWindow` to `sf::Window`, and `sfml-graphics`
     leaves the link.
4. **Glyphs.** `FontFace` (DirectWrite) replaces FreeType, and `FrontierOutpost/ext/freetype` and
   `FrontierOutpost/include/FreeType` go.
5. **Sound.** The XAudio2 mechanism moves into NeuronClient, and liblt keeps the adapter.
6. **Window and input.** `Neuron::Window` (Win32) replaces SFML's window.
   - A temporary WGL bridge in liblt, about 150 lines, keeps OpenGL drawing on the new window through
     an opaque handle, until Phase 4 deletes it. It costs little, and it keeps window and input
     faults apart from renderer faults.
   - Then `FrontierOutpost/ext/SFML` and its projects go.

**Done when:**
- there is no SFML or FreeType in the tree;
- NeuronClientTests pass in CI (WIC round trip, glyph coverage and kerning, WAV parsing, key
  mapping);
- the owner has checked text, images, sound and input in the kept apps.

### Phase 3: The Direct3D 12 core, built and tested alone

Build §5.3 in NeuronClient. The tests run on WARP, with the debug layer on:
- a draw into every format, read back and compared;
- mip chains for 2D and cube textures, odd sizes included;
- cube-face targets, and 3D UAV writes;
- barrier sequences that raise no debug-layer error;
- the descriptor ring wrapping around;
- release after the fence retires;
- PSO cache hits;
- the reflection name map, including the renamed keywords;
- one asymmetric image taken through render target, sampling and present, which pins down the §5.5
  flips.

**Done when:** every test passes on WARP in CI, with zero debug-layer errors.

### Phase 4: liblt on Direct3D 12

1. **Headers.** Engine-owned enumerations replace the GL types in the public headers. The change is
   mechanical.
2. **Shaders.** Port them to HLSL, with the registry and SMAA's HLSL porting block (§5.6).
3. **The GL layer.** Rewrite it onto `DrawContext` (§5.4, §5.5).
4. **SDF fields.** Add the instruction encoder and the compute interpreter (§5.7).
5. **Bring-up,** in this order, each step checked in an app that shows it:
   1. clear and present;
   2. interface and text (`loading`, `ui`);
   3. the camera pipeline in `war`, one pass at a time: depth prepass, G-buffer, global lighting,
      local lighting, blended, particles, lens flares, bloom, tone map, colour grade;
   4. SMAA, then the interface pass, then dither;
   5. the generators: nebula, planets, IR map;
   6. SDF meshes (`model`);
   7. plate-mesh occlusion (`platemesh`);
   8. script filters (`image`, `observatory`);
   9. the remaining apps.
6. **Deletion.** Delete OpenGL, the WGL bridge, `FrontierOutpost/ext/glew`, and
   `FrontierOutpost/include/GL` and `include/Glew`.
7. **A CI smoke job.** It runs each 3D app for N frames on WARP, offscreen, and fails on a
   Direct3D 12 error or a removed device. It uploads one PNG per app for the owner to look at. By
   N15 it lands with step 3, ahead of bring-up, and writes a downscaled copy of each frame into its
   log as well.

**Done when:** the 16 kept apps run on the owner's GPU (x64) and on the owner's ARM64 device, and
the smoke job is green.

### Phase 5: Close out

1. **Solution.** `FrontierOutpost.slnx` holds `lt`, `launch`, `NeuronClient` and
   `NeuronClientTests`.
2. **Build files.** `lt.vcxproj` and `launch.vcxproj` lose the SFML, GLEW, FreeType and OpenGL
   include directories, definitions and libraries. `.gitignore` loses its SFML negations.
3. **Documents.** The README, the migration notes, and the ADR changes of §8.
4. **Final checks.**
   - All four builds, by hand.
   - The owner's GPUs.
   - A 30-minute soak of `war`, watching memory, the descriptor rings and deferred release.

## 7. What each check can prove

| Check | Proves | Cannot prove |
|---|---|---|
| CI build, Debug\|x64 (as today) | Compiles and links | Release, ARM64 |
| NeuronClientTests on WARP | The Direct3D 12 core, DirectWrite, WIC, WAV parsing and key mapping, with no debug-layer error | Real-GPU driver behaviour |
| CI smoke run on WARP | Every 3D app renders N frames without a Direct3D 12 error; PNGs to look at | That the frames look right |
| Checkers | Formatting, naming and project shape for NeuronClient | — |
| The owner, each phase | Looks, sound, input and feel; ARM64; real GPUs | — |

If the runner has no interactive desktop, no swap chain can be created there. That is why the smoke
mode renders offscreen and never creates one.

## 8. ADRs

**New.** Numbering continues from ADR-004.

| ADR | Decision |
|---|---|
| ADR-005 | NeuronClient is FrontierOutpost's platform layer (§5.1, §5.2). |
| ADR-006 | NeuronClient builds for x64 and ARM64, amending AGENTS.md §3 (N1). `/arch` is `lt`'s: SSE2 on x64 and the default on ARM64 (N5, N10). |
| ADR-007 | Graphics is Direct3D 12, as the shared base of the NeuronClient projects: the policies of §5.3 and the conventions of §5.5. |
| ADR-008 | Shaders are HLSL, compiled into headers by FXC at SM 5.1, with reflection by `D3DReflect` (§5.6). |
| ADR-009 | SDF fields are interpreted by a compute shader (§5.7). |
| ADR-010 | Glyphs are rasterised by DirectWrite, from the font files in `GameData/font`. |
| ADR-011 | Images are decoded and encoded by WIC. |
| ADR-012 | The window and input are Win32: `WM_CLOSE` quits, system DPI awareness stays, and there is no joystick. |
| ADR-013 | What was removed as unused, and by what rule (§9). |

**Changed.**
- **ADR-001:** a scope note. NeuronClient and its tests are outside the exemption, and so is
  `FrontierOutpost/src/liblt/Shaders/` (N11), from the commit that adds its first file.
- **ADR-002:** superseded. No vendored dependency is left.
- **ADR-003:** amended. The engine's mechanism lives in NeuronClient; the adapter stays in liblt. A
  missing sound file becomes a logged warning (N6); a file XAudio2 cannot play still ends the program
  (N9).
- **ADR-004:** amended. `GameData/` holds no shaders, and the fonts are pruned.

**Runtime files (R13).** None is added for players. Screenshots stay under `cache/screenshot/`. The
CI smoke mode writes its PNG captures only where `--capture` says, and a relative path resolves
against the folder `launch.exe` works from, the one that holds `GameData/` (ADR-004). ADR-011
records it. A PSO disk cache would need its own ADR.

## 9. What goes (N4)

These lists come from the 2026-09-25 survey. Phase 1 re-verifies each item before deleting it.

**A. Apps: 12 removed, 16 kept.**
- **Removed:** `prime`, `sandbox`, `threads`, `font`, `draw`, `brain`, `hnn`, `life` (with
  `App/Life/`), `universe`, `strukt`, `colony` and `launcher`, and whatever only they reach.
- **Kept:** `war`, `dogfight`, `rails`, `ltheory`, `handling`, `observatory`, `model`, `platemesh`,
  `map`, `market`, `objectinfo`, `hud`, `ui`, `image`, `loading`, and the `widget` host.

**B. Unreachable code.**
- `FrontierOutpost/src/old/`: seven programs no project builds.
- The HTTP path: `LocationWeb` and `Location_Web`, and with them `sfml-network` and `ws2_32`.
- Joysticks: `Joystick.*`, the joystick buttons and axes, and the per-frame polling in
  `LTE/Program.cpp`.
- The C++ HUD widget, `Game/Widget/HUD.cpp`, with `Settings_Button` and the button and axis wiring
  that only it uses.
- `Config.cpp`, the archive path (`kUseArchive` is false), the Telemetry profiler branch, `Audio/`,
  `Network/`, `CodeGen/CodeBlock.h` and `CodeObject_Custom`.
- Dead functions:
  - the window setters for icon, position, fullscreen, cursor and capture;
  - `Mouse_SetPos`, `GetX`, `GetY`, `GetIdleTime`, `GetDX`, `GetDY` and `GetDP`;
  - `GetKeyChar`, `Keyboard_Block`, `Keyboard_IsBlocked`, `Keyboard_System` and `KeyWithModifiers`;
  - `CubeMap::SaveTo` and `Texture_Atlas`;
  - `SoundEngine_Null` and `CreatePhysicsEngineNull`;
  - `Renderer_DrawQuadOutline` and `GLU::*`.
- Vendored headers under `FrontierOutpost/include/` that nothing includes: `OVR/`, `enet/`,
  `GL/glut.h`, `GL/glui.h`, `GL/GLAux.h`, `UTF8/checked.h`, `Glew/GL/wglew.h` and `Glew/GL/glxew.h`.
- `sfml-audio` and `sfml-main`, which nothing links.

**C. Dead passes and shaders.**
- **Passes:** SSAO, DustClouds, MotionBlur, RadialBlur, Aberration, BloomLight, Composite and
  ClearDepth, plus the imposter renderable.
- **Shader files:** 36, 983 lines in all. 27 are referenced by nothing. 9 are reached only by the
  dead code above, the broken `ui/rect` and `solidcolor` among them.
- **SDF node types that nothing constructs.** Scripts call 3 of the 20 constructors; C++ callers
  decide the rest.

**D. Unused assets.**
- **Fonts:** keep Rajdhani, Iceland, SourceCodePro and Gafata, with their licences. Remove the other
  34 families, NotoSans, NotoSansCJKsc and Play among them, and the `FontPreview` and `SplashScreen`
  widgets that nothing opens.
- **Textures:** `icon.png` and `splash.png`.
- **Sounds:** re-checked in Phase 1.

## 10. Risks

| Risk | Where it bites | Mitigation |
|---|---|---|
| Bugs in the D3D11-style layer: barriers, lifetimes, descriptors | Phases 3 and 4 | Built and tested alone on WARP with the debug layer before liblt uses it. Release deferred by fence from the first commit. |
| GL behaviour liblt relies on without saying so: uniforms that persist, `Use()` on set, inherited attachments, units shared between programs | Phase 4 | The explicit rules of §5.4 and §5.5. Bring-up one pass at a time. |
| Upside-down images or wrong depth | Phase 4 | One macro and one present flip. The asymmetric-image test in Phase 3. |
| `d3dcompiler_47.dll` missing on a target | ARM64 | Phase 2 loads it and reflects a blob on the owner's ARM64 device. Fallback: reflection tables generated at build time. |
| FXC is frozen | Later | Nothing here needs SM 6. ADR-008 records what moving to DXC would take. |
| The SDF interpreter is slow, or shapes change | `model`, `war` | Generation time measured on WARP and a GPU. Different shapes are acceptable under N3. |
| Hitches the first time a PSO is used | The first seconds of each app | Known programs warmed at load. `ID3D12PipelineLibrary` later. |
| Modernisation runs away | Phase 4 | Only the changes named in §5 go in; everything else is backlog. |
| Mixed build settings in one DLL | Always | Warning levels are harmless. `/arch` is not: a template or inline function both libraries use keeps one copy, which the linker picks, so NeuronClient states `lt`'s `/arch` (N10). Under `/fp:precise` and `lt`'s `/fp:fast`, such a copy rounds as either library would; nothing relies on bit-exact results (MIGRATION_NOTES.md BR6). The CRT must match (`/MD`). |
| Missing-sound warnings hide broken content (N6) | Every app with sound | `Build/CheckSounds.py` lists every named sound without a WAV file, and CI runs it on every change. |
| A converted WAV that XAudio2 cannot play stops the game (N9) | The first time that sound plays | `Build/CheckSounds.py` parses every WAV file by the engine's rules and XAudio2's formats, and CI fails on one that cannot be played. |
| No automatic acceptance (N3) | Every phase | The owner signs off each phase, by the port itself (N13). |

## 11. Not in this plan

- sRGB-correct colour; HDR output; MSAA.
- Per-monitor DPI; borderless fullscreen; a vsync setting.
- A PSO disk cache; moving the glyph distance field to the CPU.
- liblt's Linux and macOS branches.
- Extracting NeuronClient into a repository of its own.

## 12. Still open for the owner

Nothing. N5–N12 settled the open items: `/arch`, missing sounds, vsync, RandomScreenshot's
background, unplayable sound files, liblt's shader folder and its calls for OS services.
