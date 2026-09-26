---
name: perf-review
description: Multi-agent performance review of FrontierOutpost (lt.dll on NeuronClient and Direct3D 12) for load time and frame rate. DirectXMath is the named focus, multithreading and Direct3D 12 changes are in scope, and the game's behaviour does not change. The lead measures a baseline on a Windows GPU, launches read-only reviewers by lens, verifies their findings adversarially and reports a ranked list. Nothing is implemented until the owner approves items by ID; each approved item then lands as its own PR. Run it only when the owner asks, because it launches many agents.
---

# Performance review: load time and frame rate

You lead a multi-agent review of why FrontierOutpost loads slowly and draws too few frames, and of
what would fix that. You take the measurements; the reviewers you launch read code. Nothing is
committed until the owner has approved it.

## 1. The brief

The owner reports long load times and a low frame rate, seen in Debug|x64 on a GPU. Find what costs
the time in `lt.dll`, NeuronClient and the shaders, and rank what would win it back. DirectXMath is
the named focus. Multithreading and Direct3D 12 improvements are in scope. The game's functionality
does not change.

The owner has already decided the following. Do not reopen them.

- **Deliverable.** A report comes first. Nothing changes until the owner approves items by ID. Each
  approved item then becomes one PR, with an ADR in the same commit where the item is a decision.
- **Equivalence bar.** Visually equivalent, as §3 defines it.
- **Where it runs.** Windows, Visual Studio 2026 and a real GPU, so findings carry measurements.
- **Configuration.** The owner saw the problem in Debug. Release|x64 is the optimisation target.
  Debug is measured too and reported as its own track, because `/Od`, `/RTC1`, the debug CRT's
  iterator checks and the D3D12 debug layer can dominate there. Phase 0 asks the owner which one
  matters if the two differ widely.

## 2. Ground rules, for the lead and every reviewer

1. **Authority.** AGENTS.md comes first, then `Design/ADR/`, then the surrounding code. Before
   proposing anything within an ADR's scope, read that ADR's "What this forecloses" section. These
   ADRs bound the work:
   - ADR-001: lt and launch are a legacy import. They keep their own names, layout and formatting,
     `/W3` and `/fp:fast`, and Debug and Release differ only as the original's did.
   - ADR-005: the layers run launch → lt → NeuronClient → SDK, and NeuronClient knows nothing of
     the game.
   - ADR-006: x64 uses `/arch:SSE2` for lt and NeuronClient alike. Nothing higher is allowed
     without a new ADR that moves both.
   - ADR-007: the Direct3D 12 core's policies.
   - ADR-008: FXC, Shader Model 5.1, reflection at load.
   - ADR-009 (SDF fields) and ADR-010 (glyphs).
   - ADR-013 decision 8: the script API is an interface.
2. **Phases 0 to 3 change nothing committed.** The lead may make local, uncommitted edits to
   instrument code or run experiments. Each one is reverted before the next measurement and named in
   the report.
3. **Only the lead builds, runs and measures.** Reviewers read code, history and the baseline. They
   never build, run, edit, commit or push. Two builds or runs at once on one machine corrupt the
   timings that every finding is ranked by.
4. **Hot or nothing.** A finding needs the baseline, or a measurement, to show that its path costs
   time in a scenario. A cost the profile does not show is not a finding, however ugly the code.
5. **Say whether a number is measured or estimated.** A measured figure says what was run, on which
   build and machine, and the median of how many runs. An estimate says its method. Never present
   one as the other (AGENTS.md §6).
6. **Out of bounds, whatever the gain.** The following may appear in the report only as owner
   decisions:
   - less quality or less content: field or texture resolution, fewer passes, SMAA, bloom, lens
     flares, particles, LOD distances;
   - other seeds or other generated content;
   - a frame cap or a change to vsync;
   - anything §3 excludes.
7. **Decisions are proposed, not made.** Some changes cross a rule: an ADR's "forecloses" list,
   AGENTS.md §3 (Debug and Release alignment, warning level, conformance, `/arch`, `/fp`), R13
   (runtime files), R14 (dependencies) or R15 (allocators, in NeuronClient). Such a change goes to
   the owner with what it buys, what it costs and an ADR outline. It is never implemented without
   approval.
8. **Ask rather than assume.** When the answer to a question would change a finding's rank or
   feasibility, the lead asks the owner. Reviewers return their questions to the lead.

## 3. The equivalence bar: visually equivalent

What the player sees and does stays the same. Generated content may differ in detail where it looks
and plays the same. That is the bar ADR-009 set for the port.

- **Must not change:**
  - every app's scripts, controls, UI and rules;
  - the objects a seed produces: how many, their types, placement, orbits and owners;
  - the render passes and their order;
  - the present path and vsync policy (ADR-007);
  - quality settings;
  - the script API and its semantics;
  - the wording of the launcher's `launch:` lines (the times in them will change), and its count
    of debug-layer errors.
- **May change:**
  - floating-point results in their last bits, since lt already builds with `/fp:fast`;
  - the fine detail of procedural textures and meshes, such as noise or asteroid surfaces, where a
    side-by-side at the game's resolution shows nothing a player would notice;
  - whatever already varies between two runs: the wall-clock timestep and `srand(time(0))`.
- **Needs the owner's sign-off, however harmless it looks:**
  - anything visible for a while, such as a placeholder or a coarser LOD while a mesh generates,
    pop-in, or a frame of stale data;
  - a change in the order of random draws that alters structure;
  - lower precision in world-space math.

**Proof for static content** (backgrounds, planets, nebulae, asteroid shapes, UI). Build twice,
with only the change between the two builds. Capture a frame from each with
`launch.exe <app> --frames N --capture <png>`. Pin `srand` with the same local, uncommitted edit in
both builds, so content driven by `rand()` matches. Report the difference between the images
(maximum and mean per channel, and how many pixels exceed a stated threshold), and look at both.

**Proof for dynamic scenes.** Moving objects never match between two runs, because `FrameTimer`
steps the simulation by wall-clock time, and its first step can span the load. The owner compares
dynamic scenes side by side, and the report says what to look at.

## 4. Phase 0: preconditions and baseline (the lead alone)

### Preconditions

- The machine has Windows, a Developer PowerShell for VS 2026 (`msbuild` on the path) and a
  Direct3D 12 GPU, not WARP. The working tree is clean, and you record the commit you measure. If
  anything is missing, stop and tell the owner.
- Use a working folder outside the repository,
  `$env:LOCALAPPDATA\FrontierOutpost-perf\<yyyy-mm-dd>\`. Logs, captures, notes and the report go
  there, and nothing in it is committed. Keep `notes.md` there, with every command you ran and what
  it printed, so the report can cite it.
- Record the machine: CPU and core count, RAM, GPU and driver version, Windows build, power plan,
  and display resolution and scaling. Close other heavy programs. Never compare timings taken on
  different machines or settings.

### Builds

- Build Release and Debug through the solution (AGENTS.md §3):
  `msbuild FrontierOutpost.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo`,
  then the same with `Debug`.
- **Symbols for profiling Release.** The committed Release build of lt and launch has no debug
  information (ADR-001, `FrontierOutpost/Directory.Build.props`). Do not edit committed files to
  get it. Instead, write this props file in the working folder and pass it to a rebuild with
  `/p:ForceImportBeforeCppTargets=<absolute path>`:

  ```xml
  <Project>
    <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
      <ClCompile>
        <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
        <AdditionalOptions>/FS %(AdditionalOptions)</AdditionalOptions>
      </ClCompile>
      <Link>
        <GenerateDebugInformation>true</GenerateDebugInformation>
        <OptimizeReferences>true</OptimizeReferences>
        <EnableCOMDATFolding>true</EnableCOMDATFolding>
      </Link>
    </ItemDefinitionGroup>
  </Project>
  ```

  The last two settings keep the linker's Release defaults, which `/DEBUG` would otherwise turn
  off. Check that `.pdb` files appear beside `lt.dll` and `launch.exe`, and that the benchmark
  times match the plain Release build within noise. If they do not match, profile with the symbols
  build and time with plain Release. Both builds write to the same folders, so run `/t:Rebuild`
  whenever you switch between them.

### Scenarios and numbers

- **Survey.** From the repository root, run each of the 16 kept apps in `GameData/script/App/` once
  in Release: `FrontierOutpost\bin\x64\Release\launch.exe <app> --frames 300`. The launcher prints
  three lines:
  - `launch: <app> initialized after A s`
  - `launch: <app> drew its first frame after B s`
  - `launch: <app> drew its last frame after C s`

  B is the time to the first frame, and (N − 1) / (C − B) is the average frame rate. The times are
  printed to a tenth of a second (`launch.cpp:162`), so choose N large enough that C − B is at
  least 10 s, or print more digits with a local edit. An app that fails exits with 1. Some apps are
  incomplete, so note it and go on.
- **Primary scenarios.** Use three apps:
  - `war`: 32 AI ships in combat, in a generated system;
  - `ltheory`: a generated universe, seed 39;
  - the slowest-loading other app from the survey.

  Run each three times with `--frames 1200`, in Release and in Debug, and report the median and
  the spread. If the spread exceeds about 5%, find out why before you measure anything else.
- **Loading does not end at the first frame.** SDF meshes and other generation run as scheduler
  jobs over the frames that follow (`Module/Scheduler.cpp`). A draw that finds no mesh for its LOD
  forces a synchronous flush of every job (`LTE/SDFMesh.cpp:187-190`). With a local edit, print a
  timestamp when the scheduler's queues first empty, and print each frame's dt. Report three
  phases:
  - the time to the first frame, split at `initialized`;
  - the warm-up, until the scheduler is idle;
  - the steady state after that.
- **Frame times, not just averages.** For the warm-up and for the steady state, report the median,
  95th percentile, 99th percentile and maximum frame time. Also report draw calls and triangles per
  frame (`Renderer_GetDrawCallCount`, `Renderer_GetPolyCount`).
- **CPU-bound or GPU-bound.** For each scenario, report CPU and GPU time per frame, how busy the GPU
  is, and where the CPU waits on a fence. The waits are `GraphicsCore::WaitFor`, and
  `Renderer_Finish`, which is `WaitIdle`.
- **Profiles.**
  - The engine's own sampling profiler samples the `FRAME`/`SFRAME` scopes, script functions among
    them by name, for one second. It prints a table to the console with the share, milliseconds per
    frame and scope path. F2 starts it in an interactive run. To automate it, call
    `Profiler_Auto(1.0f)` from a local edit of the launcher at a chosen frame. Take one profile
    during the warm-up and one at steady state.
  - Take a CPU sampling profile with symbols, for the functions inside the scopes.
  - Take a PIX GPU capture of one steady-state frame of `war`, for the time per pass.
  - Where a tool needs a person at the GUI (PIX, WPA, the Visual Studio profiler), write the owner
    exact steps: the build, the app, the moment to capture, and what to export. Wait for the result,
    and never guess what a capture would show.
- **The Debug gap.** Take the same numbers in Debug, and attribute the difference between the two
  builds:
  - the D3D12 debug layer, which is on in every Debug run (`LTE/RendererCore.cpp:352-356`).
    Measure its share with a local edit that turns it off. The log says "with the debug layer" when
    it is on;
  - `/Od` without inlining, and `/RTC1`;
  - the debug CRT's iterator checks on the containers in the per-draw path.
- **Gate.** If Debug is more than about three times slower than Release in load time or frame rate,
  show the owner both sets of numbers. Ask which configuration they want improved before Phase 1,
  and weight the lenses by the answer.

Write `baseline.md`, with:

- the machine, the commit and the commands;
- the tables;
- the top 20 CPU scopes and functions in each phase;
- the most expensive GPU passes;
- the CPU-bound or GPU-bound verdicts;
- the Debug attribution.

Every reviewer gets its summary.

## 5. Phase 1: the lens reviews (parallel and read-only)

Launch one reviewer per lens with the Agent tool, using general-purpose agents. Put all the calls in
one message so that the reviewers run at once. Do not use Explore agents: they locate code but do
not audit it. Each reviewer's prompt holds these, verbatim:

- §2 and §3;
- the shared brief below;
- the summary of `baseline.md`;
- the lens's brief;
- the finding format.

You may merge or drop a lens that the baseline shows cannot matter, such as GPU work when the GPU
sits idle for most of the frame. The report says which lens and why. The reviewers' reports reach
you, not the owner, so relay what matters.

### Shared brief

You are one reviewer in a multi-agent performance review of FrontierOutpost. Read AGENTS.md, and
the ADRs your lens touches, before you judge anything. You read code, history and the baseline. You
never build, run, edit, commit or push, because the lead alone measures.

`launch.exe` runs an LTSL app script from `GameData/script/App/`. It loads `lt.dll`, known as
liblt: the 2012–2015 Limit Theory engine, a legacy import (ADR-001) built with `/W3 /fp:fast
/arch:SSE2`. liblt reaches graphics, glyphs, images, sound and input only through
`NeuronClient.lib`. NeuronClient is new code under AGENTS.md in full: `/W4 /WX /fp:precise
/arch:SSE2`, clang-tidy and clang-format (ADR-005). Graphics is Direct3D 12 at feature level 11_0.
Shaders use Shader Model 5.1, are compiled by FXC into headers and are reflected at load (ADR-007,
ADR-008).

- **The main loop is single-threaded** (`LTE/Program.cpp`). Each frame runs input, the window, the
  app's `OnUpdate` (the script's `Update`, physics and sound), then `Module_UpdateGlobal` (the
  scheduler, frame timer and profiler), then `Display` and present.
- **Scripts.** Game logic, UI and HUD are LTSL scripts, run by an AST-walking interpreter
  (`LTE/Expression/`, `LTE/ScriptFunction.cpp`).
- **Rendering.** liblt's renderer (`LTE/Renderer.cpp`) is GL-era and immediate: state lives on
  stacks, and shader uniforms are set by name (`LTE/Shader.cpp`). It drives `Neuron::DrawContext`
  (`NeuronClient/DrawContext.cpp`), a D3D11-style immediate context over Direct3D 12:
  - a cache of pipeline states, keyed by state;
  - barriers tracked per subresource;
  - a descriptor ring per frame, and an upload ring;
  - releases deferred until a fence passes;
  - two frames in flight on one direct queue.

  Deferred shading passes live in `Game/RenderPass/` and post filters in `LTE/RenderPass/`, with
  SMAA. Everything renders offscreen, and a present pass flips the image into the swap chain. Vsync
  is off and tearing is on.
- **`Renderer_Finish()` is `WaitIdle()`.** The CPU waits until the GPU has finished all its work
  (`LTE/RendererCore.cpp:340`).
- **Jobs** (`Module/Scheduler.cpp`). Serial jobs run on the main thread for up to 1/60 s per frame.
  Each threaded job gets a new `std::jthread`, with no pool: at most 8 at a time, and 1 MB of
  declared memory.
- **Single-threaded state.** Reference counts (`RefCounted`, `Reference<T>`) are not atomic
  (`LTE/Thread.cpp:31`). The script interpreter and `DrawContext` are single-threaded.
- **Time.** `FrameTimer` (`Module/FrameTimer.cpp`) gives a variable dt from the wall clock, and
  physics uses it.
- **Chance.** `srand(time(0))` runs at startup (`LTE/Program.cpp:17`), and `rand()` feeds content,
  for example `Game/Object/System.cpp:187`. Apps seed `RNG_MTG` explicitly: `war` uses 0, `ltheory`
  uses 39. The MSVC CRT keeps `rand()`'s state per thread, so a worker thread starts at seed 1.
- **Math.** liblt has only scalar templates (`LTE/V2.h`, `V3.h`, `V4.h`, `Matrix.h`,
  `Quaternion.h`, `Bound.h`, `Transform.h`, `StdMath.h`), and no SIMD or DirectXMath anywhere.
  `DistanceT` is `double` (`LTE/Common.h:59`), so world positions and world matrices are double
  precision (`Game/Common.h:41-44`).

The leads in your brief are places to check, not conclusions. Confirm each one against the baseline
and the code, or dismiss it.

### Finding format

Return findings in this form, most valuable first:

```
### <LENS>-<n>: <title>
- Where: <file:line>, reached from <call path from a frame root, e.g. Program::Execute → Display>
- Scenario: load | warm-up | steady state | hitch; <apps>
- Mechanism: <why it costs time, in one paragraph>
- Evidence: <the baseline data that shows it is hot, or "not in the baseline: needs <measurement>">
- Expected gain: <a range, marked measured or estimated, with the method>
- Change: <what would change, as a sketch, not a patch>
- Equivalence: <what the player could see differently, and how §3's proof would show it does not>
- Concurrency: <threading changes only: what crosses threads, reference counts, rand(), scripts,
  DrawContext>
- Rules: <the AGENTS.md sections and ADRs touched; "owner decision" if it crosses one (§2.7)>
- Effort: S | M | L    Confidence: high | medium | low
```

After the findings, add three sections:

- **Questions for the owner.**
- **Measurements requested from the lead:** what to run, and what result would confirm or refute
  the finding.
- **Leads checked and dismissed:** one line each, with the reason.

### Lens briefs

**SYNC: CPU and GPU synchronisation, and frame pacing.** Scope: every point where the CPU waits for
the GPU, or the GPU for the CPU, at load and in each frame, and how well the two overlap. Leads:

- `Module/Scheduler.cpp:139,154`. `Renderer_Finish()` runs before the first serial job of a frame
  and after every job run. So every frame with generation work drains the GPU, and each job's timing
  includes GPU time. ADR-009 decision 4 relies on that wait to have a LOD grid's readback complete
  by the job's next run. A change must keep that guarantee another way, for example with a fence
  per job.
- `LTE/SDFMesh.cpp:187-190`. A draw that finds no mesh for its LOD flushes the whole scheduler
  inside the frame.
- Generation in strips, with a full wait after each strip and the strip width adapted from
  CPU-measured time: `LTE/Texture2D.cpp:40-60`, `LTE/CubeMap.cpp:122-145`,
  `LTE/Renderer.cpp:473-487` (`Renderer_DrawFSQInParts`) and `LTE/PlateMesh.cpp:176-192`. The split
  keeps each GPU submission under the Windows TDR limit, as the code's comments say and `814b6fe`
  did for the occlusion bake, so it stays. What can go is the CPU wait between strips: GPU
  timestamps can size the strips, and submissions can queue without a wait.
- `NeuronClient/DrawContext.cpp:781-820` and `:822-848`. The descriptor ring waits for the GPU when
  it is full, and a full sampler heap calls `SubmitAndWait` (`:830`). Find how often each happens.
- `Game/Graphics/Generator/IRMap.cpp:26,65`: a synchronous readback (`GetData`) per face.
- Submissions per frame: `Renderer_Flush` in `UI/Widget/Rendered.cpp:143` and
  `LTE/Profiler.cpp:128,152`.
- The present path (`LTE/Window.cpp:165`, `NeuronClient/SwapChain.cpp:238`) and the device's frame
  begin and end. Where does the CPU block at the frame boundary, and does the GPU ever sit idle
  while it waits for the next frame's commands?

Constraints: ADR-007's policies stay unless proposed as a decision. Those are two frames in flight,
one direct queue, synchronous readbacks at load and asynchronous ones per frame, and the present
path. Protection against TDRs stays.

**D3D12: NeuronClient's CPU cost per draw, and its use of Direct3D 12.** Scope: `DrawContext.cpp`,
`GraphicsDevice.cpp`, `GraphicsCore.cpp`, `Program.cpp`, `Texture.cpp`, `Buffer.cpp` and
`SwapChain.cpp` in `NeuronClient/`. Leads:

- `PrepareDraw` (`DrawContext.cpp:892` onwards). Every draw builds a
  `std::vector<D3D12_INPUT_ELEMENT_DESC>`, builds an input-layout key as a `std::string` with
  `std::format`, and looks it up in a `std::map<PipelineKey, …>` that compares that string
  (`:131-145`, `:373`).
- `PrepareTables` (`:849-890`). Every draw builds a table of 16 `D3D12_SAMPLER_DESC` and looks it
  up in a `std::map` (`:429`). Each new table of views costs 16 `CopyDescriptorsSimple` calls
  instead of one `CopyDescriptors`.
- Every draw iterates `program->samplers` and `program->shaderResources`, which are held by name.
- Barrier tracking (`Require`, `:587-660`): a vector of states per subresource, and how barriers
  are batched into `ResourceBarrier` calls.
- Constants (`:1220-1290`). Each draw copies each stage's constants into the upload ring. Also
  check the upload ring's page size (`uploadPageBytes`).
- Pipeline states are created at first use, and nothing warms them. The plan names warming the known
  programs at load as the mitigation (`Design/Archive/NeuronClient-migration.md` §10), and it does not
  exist. `ID3D12Device`'s methods are free-threaded, so pipeline states can be created on workers.
- Command allocator and list reuse, the number of submissions, and how often the heaps and the root
  signature are re-bound per list.

Constraints:

- AGENTS.md applies in full: the naming table, R12 (`ComPtr` RAII), no `d3dx12.h`, R14, and R15
  (no pool allocator without an ADR).
- The layer stays game-agnostic (R9), and its public headers stay free of Windows and Direct3D
  headers (ADR-005 decision 5).
- Every change keeps or adds coverage in NeuronClientTests, which run on WARP with the debug layer.
- Two items are owner decisions. Warming pipeline states at load changes ADR-007's "created at
  first use". A disk cache (`ID3D12PipelineLibrary`) needs its own ADR, and its file is a runtime
  file under R13.

**RENDER: liblt's renderer and passes.** Scope:

- `LTE/Renderer.cpp`, `RendererCore.cpp`, `Shader.cpp`, `ShaderInstance.cpp`, `DrawState.cpp`;
- the draw paths in `Mesh.cpp`, and `ParticleSystem.cpp`;
- `Game/RenderPass/`, `LTE/RenderPass/` and `UI/WidgetRenderer*.cpp`;
- the GPU cost of the shaders in `FrontierOutpost/src/liblt/Shaders/`.

Leads:

- `Shader.cpp:109` and from `:308`. Every `SetFloat("name", …)` builds a `String` and looks it up
  in a `Map`. The `(*shader)("name", value)` chains do this for each uniform on each draw.
- `Renderer.cpp:781-796`. Every change to the world matrix computes a general 4×4 inverse and
  transpose (`worldIT`) and two matrix products. `InjectMatrices` (`:415-422`) then binds five
  matrices per draw.
- The transient draw paths (`Renderer.cpp:393`, `:558`, `:636`) upload geometry and convert indices
  on every draw. Which of these draws could be static?
- `Game/RenderPass/Visibility.cpp:18-50` culls each object recursively every frame, and calls
  `Cullable::Recompute` for each one.
- `LTE/ParticleSystem.cpp:69-130` builds particle vertices on the CPU every frame.
- The UI calls `WidgetRenderer_Flush` (`UI/WidgetRenderer.cpp:276`) from many places. Count the
  flushes and draws per frame in the HUD.
- GPU time per pass, from the PIX capture: render-target formats and sizes (bandwidth), full-screen
  passes, SMAA's three passes, bloom, lens flares, and local lights
  (`Game/RenderPass/LocalLighting.cpp`).

Constraints:

- lt is legacy (ADR-001): match the surrounding style, with no renames and no reformatting.
- Shaders fall under AGENTS.md (ADR-008) and compile with FXC at Shader Model 5.1, so there are no
  wave intrinsics and no Shader Model 6.
- A shader change keeps the output visually equivalent. Lowering precision or resolution is out of
  bounds (§2.6).

**LOAD: the load path and content generation.** Scope: everything from process start to the first
frame, and on until the scheduler is idle. That covers loading and compiling scripts, shader
programs and reflection, textures, sounds, fonts, and procedural generation. Leads:

- **Scripts.** `ScriptFunction_Load` (in `launch.cpp`), `LTE/Script.cpp`, `LTE/LTSL.cpp` and
  `LTE/Expression/` load and compile the scripts. Caching them on disk is compiled out
  (`LTE/ScriptFunction.cpp:6` and its `#if 0` block). Turning it on would add a runtime file (R13,
  ADR-004), with the caveats in `Design/Archive/MIGRATION_NOTES.md` H5 and H6. That is an owner
  decision.
- **Shader programs:** the registry lookup, and one `D3DReflect` per program at load
  (`LTE/ShaderRegistry.cpp`, `NeuronClient/Program.cpp`).
- **Textures and sounds.** Textures come through WIC (`NeuronClient/ImageFile.cpp`,
  `LTE/Texture2D.cpp`) and sounds through `NeuronClient/SoundBuffer.cpp` and
  `Module/SoundEngine/XAudio2.cpp`. Is all of it on the main thread?
- **Glyphs** (`LTE/Font.cpp:60-135`). Each glyph costs a DirectWrite rasterisation, an upload and a
  GPU distance-field pass (ADR-010).
- **Procedural generation:**
  - textures and cube maps: `Game/Graphics/Generator/`, `LTE/Texture2D.cpp`, `LTE/CubeMap.cpp`;
  - plate meshes: `LTE/PlateMesh.cpp`;
  - SDF fields: `LTE/SDFMesh.cpp` and `Shaders/GenFieldCS.hlsl`. ADR-009 measured 22 s per 128³
    field on WARP in Debug, and the figure on a GPU has not been taken;
  - polygonisation: `Volume/MarchingCubes.cpp` and `Volume/Contour.cpp`;
  - collision meshes: `LTE/CollisionMesh.cpp` and `ThirdParty/KDTree.cpp`.
- Which of these tasks are independent and could overlap, for example CPU work on workers while the
  GPU generates?

Constraints: generated content stays equivalent (§3), so resolutions, seeds and what is generated
do not change. Protection against TDRs stays.

**THREADS: the threading model and parallel work.** Scope: `Module/Scheduler.cpp`, `LTE/Thread.cpp`,
`LTE/Lock.cpp`, `LTE/Job.h`, every threaded job (`Scheduler_Add(…, true)`), and any CPU-heavy work
from the other lenses' leads that could run in parallel. Leads:

- Each threaded job gets a new `std::jthread`, with no pool, and `Terminate` detaches it
  (`LTE/Thread.cpp`). The scheduler allows at most 8 threads and 1 MB of declared memory
  (`Module/Scheduler.cpp:15-21`).
- Serial jobs share the main thread's frame, with a budget of 1/60 s.
- Candidates to run in parallel:
  - polygonisation, chunk by chunk;
  - mesh normals and bounds;
  - building collision meshes and k-d trees;
  - decoding images and WAV files;
  - rasterising glyphs;
  - creating pipeline states;
  - per-object culling and simulation updates.

Each finding must answer these points:

- **Reference counts are not atomic.** No `Reference<T>` may be copied or released on two threads
  at once. Say who owns every object that crosses a thread, and when it is released.
- **`rand()` state is per thread.** Code that calls `rand()`, the `Rand*` functions in
  `LTE/Math.h`, or the random picks of `Array` and `Vector` gets a different sequence on a worker.
  Moving that code off the main thread changes content, which needs the owner's sign-off (§3).
- **Single-threaded systems stay on one thread.** The script interpreter, the renderer and
  `DrawContext` are single-threaded, and a worker must not call into them. Recording commands on
  several threads needs several command lists and contexts. That redesigns ADR-007's core and is an
  owner decision.
- **Standard library only:** `std::jthread`, `std::mutex`, `std::atomic`,
  `std::counting_semaphore`. No new library (R14). A thread pool written in liblt follows liblt's
  style (ADR-001); one in NeuronClient follows AGENTS.md.
- **Deterministic merging.** Where results must be deterministic, say how. Merge them in a fixed
  order, not in the order they complete.

**MATH: DirectXMath and CPU math.** Scope: all float math on hot CPU paths, and where DirectXMath
(`DirectXMath.h`, `DirectXCollision.h`, `DirectXPackedVector.h`) would make it measurably faster.
Facts:

- **DirectXMath adds no dependency.** It is header-only and ships in the Windows SDK, the baseline
  R14 allows. Its default x64 path uses SSE2, which matches `/arch:SSE2`, and on ARM64 it uses NEON.
  Its SSE3, SSE4, AVX, AVX2, FMA3 and F16C paths need instructions above the SSE2 floor. That is
  ADR-006 territory: it takes a new ADR that moves lt and NeuronClient together.
- **Storage types stay as they are.** `V2`, `V3`, `V4`, `Transform` and `Bound` are `AutoClass`
  types that the LTSL type system and serialisation see. Their size, alignment and members are part
  of the script ABI. Never store `XMVECTOR` or `XMMATRIX` in liblt objects: they need 16-byte
  alignment, which liblt's own allocators (`LTE/Pool.h`, `LTE/StackAlloc.h`) may not give. Use
  DirectXMath inside hot functions: load, compute, store. `XMFLOAT3`, `XMFLOAT4` and `XMFLOAT4X4`
  match `V3`, `V4` and `Matrix` in size; prove it with `static_assert`.
- **Double precision stays double.** `Position`, `WorldMatrix`, `Bound3D` and `RayD` are double.
  DirectXMath is float-only, so never move world-space double math to float.
- **The matrix convention.** `MatrixT::e` holds a column-vector (M·v) matrix in column-major order,
  with the translation in `e[12..14]` (`LTE/Matrix.h:17-26`, `:216`). The "ROW-MAJOR!" comment at
  `:11` does not describe this. HLSL reads it as a `float4x4` with default column-major packing and
  `mul(M, v)` (`Shaders/Common.hlsli:117`). Loaded unchanged with `XMLoadFloat4x4`, it is the
  equivalent DirectXMath row-vector matrix. So liblt's `A * B` is `XMMatrixMultiply(B, A)`,
  `TransformPoint` is `XMVector3Transform`, and `TransformVector` is `XMVector3TransformNormal`.
  Treat this as a claim, and prove it with a test before any substitution.
- `LTE/StdMath.h` computes every float function through double: `(float)sqrt((double)t)`, and the
  same for `Sin`, `Cos`, `Exp`, `Log`, `Pow`, `Floor`, `Acos`, `Abs` and the rest.
- **Estimate functions lose precision.** Use `XMVectorReciprocalEst`, `XMVector3NormalizeEst` and
  `XMScalarSinEst` only where the result is drawn and stays visually equivalent. Never use them in
  physics, orbits or anything that accumulates.

Leads. Check each against the profile: most per-frame matrix work takes microseconds and will not
pay.

- Bulk loops:
  - `Volume/MarchingCubes.cpp` and `Volume/Contour.cpp`;
  - `LTE/Mesh.cpp`: normals, transforms and bounds;
  - `LTE/CollisionMesh.cpp` with `ThirdParty/TriTriOverlap.cpp` (`DirectX::TriangleTests`);
  - `LTE/SpatialPartition_*.cpp`.
- Culling in `Game/RenderPass/Visibility.cpp`, with `BoundingFrustum` and `BoundingSphere` from
  DirectXCollision. They are float, so use them only on camera-relative data.
- The vertex build in `LTE/ParticleSystem.cpp`, and the per-draw matrix work in
  `Renderer.cpp:781-796`.
- Texel conversion that involves half floats (`ConvertTexels`, `LTE/RendererCore.cpp:187`), with
  DirectXPackedVector.
- The double round trips in `StdMath.h`, where the profile shows them.

Constraints:

- Include the DirectXMath headers only in the `.cpp` files that use them, or in one small adapter
  header. Never include them in `V3.h`, `Matrix.h` or `Common.h`. That keeps compile times down and
  keeps clear of liblt's names that collide with Windows macros (MIGRATION_NOTES.md H3, ADR-005's
  context).
- Follow DirectXMath's calling-convention rules in any function that takes its types: `XM_CALLCONV`,
  and `FXMVECTOR`, `GXMVECTOR`, `HXMVECTOR` and `CXMVECTOR`, or `FXMMATRIX` and `CXMMATRIX`, by
  parameter position.
- The code still compiles for ARM64 on the NEON path. Make no performance claim for ARM64 (ADR-006
  decision 6).
- Adopting DirectXMath in lt is a decision. The first change carries an ADR (the next free number,
  ADR-014 when this skill was written) that states the storage, precision and `/arch` rules above.
- Report the candidates that do not pay, with numbers, so the question is closed.

**SIM: the script interpreter, simulation and UI on the CPU.** Scope: the CPU work in a frame
outside rendering:

- the interpreter: `LTE/ScriptFunction.cpp`, `LTE/Expression/`, `LTE/Data*.h`, `LTE/Type*`;
- the simulation: `Game/Object*.cpp`, `Component/`, `Game/Task/`, `AI/`,
  `LTE/SpatialPartition*.cpp`, `Game/Object/Scanner.cpp`, `Module/PhysicsEngine.cpp`;
- the UI widgets in `UI/`, and the scripts under `GameData/script/Widget/`.

Leads:

- `LTE/ScriptFunction.cpp:37-38`. Every script call constructs an `Environment` and reserves 32
  registers, a heap allocation per call.
- Dynamic dispatch, `Data` boxing and conversion, and string-keyed lookups while expressions are
  evaluated.
- The updates for each of `war`'s 32 AI ships: tasks, scanners and spatial queries.
- The HUD (`GameData/script/Widget/HUD/`), and its text and widget work every frame.

Constraints: script semantics and the script API do not change (ADR-013 decision 8). An
optimisation inside the interpreter must give every script the same results. No `.lts` file
changes, unless the behaviour stays identical and the owner approves it.

**CONFIG: build configuration and costs that only Debug has.** Scope:
`FrontierOutpost/Directory.Build.props`, `lt.vcxproj`, `launch.vcxproj` and `NeuronClient.vcxproj`,
and the Debug attribution from the baseline. Leads:

- Release has no LTCG (`WholeProgramOptimization` is false) and no debug information. lt uses
  `/fp:fast` and `/arch:SSE2`, and NeuronClient uses `/fp:precise`.
- Debug uses `/Od` without inlining, `/RTC1` and the debug CRT with iterator debugging. The D3D12
  debug layer is on for every Debug run (`LTE/RendererCore.cpp:352-356`).

Constraints: every change in this lens is a decision. ADR-001 fixes how Debug and Release differ
for lt and launch, AGENTS.md §3 fixes the rest, and ADR-006 fixes `/arch` and `/fp`. Propose each
change with its measured effect and an ADR outline, and implement none without approval. Options to
evaluate include:

- LTCG in Release;
- a switch to run Debug without the debug layer;
- `/Ob1` in Debug;
- a profiling configuration with Release code and symbols.

## 6. Phase 2: consolidate, verify, measure

1. **Merge.** Make one finding per root cause across the lenses, and keep every original ID as an
   alias.
2. **Drop what is not hot.** Drop a finding whose path the baseline does not show hot, unless it
   shows why the baseline cannot see the cost (a hitch between samples, for example). List what you
   dropped, with the reason.
3. **Verify adversarially.** For each remaining finding, launch a fresh general-purpose agent. Give
   it the finding, §2, §3, the shared brief and the baseline summary, and ask it to refute the
   finding:
   - Is the path hot?
   - Is the mechanism right?
   - Would the change be visually equivalent?
   - Does it break a threading invariant, or cross an ADR?

   The verdict is CONFIRMED, PLAUSIBLE or REFUTED, with the reasons. Run up to six verifiers at a
   time, in parallel. A PLAUSIBLE finding needs a measurement before it can rank high.
4. **Measure.** Take the CONFIRMED and PLAUSIBLE findings with the largest expected gains. Run the
   measurements the reviewers asked for, and upper-bound experiments where they are cheap: remove
   the cost temporarily, even incorrectly (skip a wait, for example), to see the most that fixing it
   could gain. Run one experiment at a time, locally and uncommitted, on Release, taking the median
   of three runs, and revert it afterwards. Drop a finding whose upper bound is within noise.
5. **Ask.** Merge the reviewers' open questions and put them to the owner before you write the
   report.

## 7. Phase 3: the report, then stop

Write `report.md` in the working folder, and give it to the owner in full. It has these sections:

1. **Baseline.** The numbers for Release and Debug. Where the time goes during the load, the
   warm-up and the steady state. Whether each scenario is CPU-bound or GPU-bound.
2. **Two ranked lists, one for load time and one for frame rate.** Rank by gain in the scenarios
   the owner cares about, then by confidence, then by effort. Start with a table: ID, title,
   scenario, gain (measured or estimated), confidence, risk to equivalence, rules touched, effort.
   Then give each item in the finding format, with its verification verdict.
3. **Owner decisions.** Every item that crosses an ADR or an AGENTS.md rule, with its measured
   effect and an ADR outline: context, decision, and what it forecloses.
4. **The Debug track,** if the owner asked for it.
5. **The DirectXMath verdict.** What pays and what does not, with numbers.
6. **Dismissed and refuted findings,** one line each.
7. **The proposed order of work,** one PR per item, with the dependencies between items.
8. **What was measured, what was estimated, and what was not verified.**

Then stop. Nothing is implemented until the owner approves items by ID.

## 8. Phase 4: implementing what the owner approved

For each approved ID, in the agreed order:

1. **One branch and one PR per item.** Branch off `main`, and open one PR per item (AGENTS.md §6).
   If the item is a decision, its ADR, with the next free number in `Design/ADR/`, goes in the same
   commit as the change.
2. **Code.**
   - Changes to lt match the surrounding legacy code (ADR-001).
   - Changes to NeuronClient and its tests follow AGENTS.md in full.
   - New files are registered in the `.vcxproj` and the `.filters`.
   - No new dependency, and no silenced warning.
3. **Tests.** A NeuronClient change keeps or adds coverage in NeuronClientTests. liblt has no test
   project. If a change needs one (the matrix-convention test, for example), ask the owner before
   adding it, because a new project is a decision. Never put liblt code in NeuronClientTests (R9).
4. **Verify before pushing:**
   - Build Debug|x64 and Release|x64 through the solution.
   - Run `python Build\CheckFormat.py`, `python Build\CheckProjectFiles.py` and
     `python Build\RunClangTidy.py`.
   - Run vstest over every suite.
   - Run the benchmark scenarios on the baseline machine, taking the median of three. Compare with
     the baseline and with the previous PR.
   - Give §3's proof.
   - Launch every app the change can reach, and look at it.
   - Run those apps in Debug on the GPU with `--frames`, with the log confirming the debug layer is
     on. A run fails on any debug-layer error.
   - For changes to SIMD code or NeuronClient, compile ARM64 if its tools are installed. Otherwise,
     say that ARM64 was not built.
5. **The PR description** gives the before and after numbers with their commands, and the
   equivalence evidence. It also says what was verified, what was assumed, and any rule that had to
   bend.

## 9. Stop and ask when

- a finding's value depends on what the owner wants: Debug or Release, quality or speed;
- the equivalence of a change is in doubt;
- a change would cross an ADR or an AGENTS.md rule;
- a benchmark gets slower beyond noise in any scenario, or the debug layer reports a new message;
- measurements vary by more than about 5% between runs. Find out why before you rank anything.
