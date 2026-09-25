# ADR-007: Graphics is Direct3D 12, as the shared base of the NeuronClient projects

- **Status:** Proposed (2026-09-25, NeuronClient migration Phase 0), for the owner to accept
- **Scope:** NeuronClient's graphics core (`GraphicsDevice`, `SwapChain`, `Texture`, `Buffer`,
  `Program`, `DrawContext`), and `lt.dll`'s renderer on top of it
- **Detail:** `Design/Plan/NeuronClient-migration.md` §2, §3 (point 1), §4.2, §5.3, §5.5, §11, N0,
  N3 and N7. This is the graphics decision AGENTS.md R12 asks for.

## Context

liblt renders through OpenGL 2.1, in a 2013 design (plan §3 point 1, §4.2):

- render state is implicit, pushed and popped on stacks;
- uniforms are set by name and persist per program, and textures bind to units when their uniform
  is set;
- `glGenerateMipmap` runs on every texture it creates;
- readbacks are synchronous, lens-flare visibility among them every frame, and `glFinish` runs at
  every present;
- resources are created and destroyed mid-frame.

Direct3D 12 provides none of that. NeuronClient must first build a D3D11-style layer: state
tracking and barriers, descriptor rings, a pipeline-state cache, upload and readback rings, release
deferred until the GPU is done, and a compute mip generator. It is the largest single piece of new
code, and Direct3D 11 would have supplied it. The owner's brief names Direct3D 12 all the same
(N0), and **it holds up only as the shared base of the owner's NeuronClient projects** (plan §3,
point 1). This ADR records that reason so it is not reopened.

N3 lifts parity with the OpenGL build as the acceptance bar. The port takes the cheaper Direct3D 12
route wherever emulating OpenGL would cost more, and nothing beyond that (plan §2).

## Decision

1. **Direct3D 12, at feature level 11_0 and Shader Model 5.1.** That covers every Direct3D 12 GPU
   and WARP, and keeps Windows 10 (plan §5.3). OpenGL, GLEW and the temporary WGL bridge are
   deleted once liblt runs on it (plan Phase 4 step 6).
2. **The core's policies** (plan §5.3):
   - two frames in flight on one direct queue, with no per-frame wait for the GPU;
   - two root signatures built in C++: one for every liblt program (a root CBV at `b0`, an SRV
     table `t0`–`t15` and a sampler table `s0`–`s15`), and one for compute;
   - views in CPU-only heaps, a shader-visible CBV/SRV/UAV ring per frame, and one shader-visible
     heap of at most 2,048 samplers, its tables deduplicated by content. Static samplers would not
     do: the SDF field's border colour is red;
   - pipeline states cached under program, blend, depth, cull, target formats and count, depth
     format, input layout and topology, and created at first use;
   - barriers tracked per subresource and inserted by the context;
   - every release held until its fence retires;
   - a per-frame upload ring, and a staging buffer of its own for each large upload;
   - readbacks synchronous for load-time work and screenshots, and asynchronous per frame: the
     lens flares read the newest completed result, one or two frames old;
   - mips from a compute downsampler, when liblt asks for them on a texture it samples with mips,
     not on every creation;
   - any colour texture usable as a render target, and one with mips as a UAV too;
   - on device removal, DRED data to the log and an exit through the fatal path;
   - the debug layer in Debug, GPU-based validation behind a switch, and PIX markers without
     WinPixEventRuntime (AGENTS.md R14).
3. **How the frame reaches the window.** Everything renders offscreen, and a present pass copies
   it to the back buffer, making the one vertical flip. The swap chain is flip-discard with two
   buffers, and resizes through `ResizeBuffers`. **Vsync is off, with tearing where supported**
   (N7), as `FrontierOutpost/src/launch/launch.cpp:41` asks. **Content renders at the client
   area's size, one pixel to one pixel,** as it does today: the passes size their targets from the
   frame and recreate them when it changes size. Nothing is authored at a fixed resolution and
   scaled (plan §5.3).
4. **OpenGL's conventions stay, without OpenGL** (plan §5.5). Images keep row 0 at the bottom.
   Every vertex shader ends in one macro that negates clip-space y and maps z from [−w, w] to
   [0, w], so depth and clipping match GL's; the front face flips with the winding. Viewports and
   scissors keep liblt's numbers, `gl_FragCoord` becomes `SV_Position`, and readbacks keep liblt's
   CPU flips.
5. **The context turns GL's implicit behaviour into rules** (plan §5.5). The colour formats map to
   their UNORM and FLOAT equivalents and depth to `D32_FLOAT`, with no sRGB: the game does its own
   gamma. A sampler is chosen at bind time from the texture's settings. A draw binds depth only
   when it tests or writes it, and only as many targets as its program writes. Alpha and additive
   blending use one state for all targets.

## What this forecloses

- **Any other graphics API for this game,** Direct3D 11 included, although it would have supplied
  the layer above. The reason for Direct3D 12 is the shared base, not this renderer's needs.
- **Parity with the OpenGL build.** SMAA's output changes, for one: GL uploaded its search texture
  sheared, and the port uploads it correctly (plan §4.2).
- **Converting to Direct3D's top-left origin during the port.** A wrong flip is the likeliest
  porting bug and the hardest to see across 133 shader files, so each convention lives in one
  macro and one pass. Converting later, pass by pass, stays possible (plan §5.5).
- **Recovery from device removal:** liblt generates too much GPU content at run time to rebuild it.
- **Exclusive fullscreen,** which nothing uses. Borderless fullscreen, a vsync setting, sRGB, HDR,
  MSAA and a PSO disk cache are backlog (plan §11), and the disk cache would need its own ADR
  (plan §8).
