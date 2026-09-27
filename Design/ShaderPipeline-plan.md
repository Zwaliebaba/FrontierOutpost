# Shader pipeline: effects, pipeline states and bindless parameters

- **Status:** Proposed 2026-09-27. The owner answered four scoping questions the same day (§0),
  and approved P0.1 to P0.6. Those landed together, as the owner asked, one commit each on
  `claude/vibrant-planck-8jxlx1`. CI builds them at Debug|x64 and passes every test on WARP; none
  has been run in the game, and each P0 item in §4 says what it still needs from a run. Nothing
  from P1 on is approved. The owner approves items by ID; from P1
  on each lands as its own PR, with its ADR in the same commit where the item is a decision
  (AGENTS.md §6). ADR numbers are the next free ones when each lands; the performance plan's D
  items take numbers from the same sequence.
- **Scope:** how a shader becomes a draw or a dispatch:
  - `Program`, and `DrawContext`'s pipeline-state cache, root signatures and binding;
  - the two shader registries;
  - `Shader`, `ShaderInstance`, `DrawState`, and the render styles that set program constants;
  - the LTSL functions that reach them;
  - the declarations in the HLSL, not its bodies.
- **Files:** C++ and shader references give the file name, which is unique in the tree. Scripts are
  under `GameData/script/`. Line numbers are those of `f884ca3`, the commit reviewed: P0 has since
  moved some, and removed the code H3's point to.
- **Source:** a read-only review of `f884ca3` on 2026-09-27. Nothing was built or run: the session
  had no Windows, MSBuild or GPU. Every cost below is an estimate and says how it was reached. §9
  separates what was checked by reading, what Microsoft's documentation confirms, and what needs a
  run.
- **Bounds:**
  - The game's behaviour does not change. OpenGL's conventions stay (ADR-007 decision 4).
  - The output bar per phase:
    - every item before P5 is bit-exact, as the perf-review skill's §3 defines it, except P0.6,
      which fixes a bug and changes output only where the bug shows;
    - P5, the compiler switch, is visually equivalent;
    - P6 is bit-exact against P5.
- **Relation to `Design/ShaderPerformance-plan.md`:** that plan goes first (§0, answer 3). §7
  lists where its items and these meet.

## 0. The owner's answers (2026-09-27)

1. **Scope: the binding model only.** Pipeline states are declared and made before first use,
   parameters are typed data, and nothing is reflected at run time. Bytecode stays compiled into
   `FrontierOutpost.exe`; no shader file is read at run time.
2. **Floor: DXC at Shader Model 6.6, bindless.** Resources are reached through
   `ResourceDescriptorHeap` and `SamplerDescriptorHeap`. As P3, P5 and P6 land, this supersedes:
   - ADR-007 decisions 1 and 2 (feature level 11_0, SM 5.1 and Windows 10; the root signatures,
     descriptor tables and sampler heap);
   - ADR-008 decisions 1, 3, 4 and 9, and its "forecloses DXC" clause.

   §6 maps each supersession to its phase. §2 point 3, and O1 to O3, say what it costs.
3. **Order: the performance plan's items first.** A correction to the question as it was asked: it
   called E1 to E9 approved. That plan's status line approves M1, M2, and D1 to D5 and D7 to D9;
   the E and L items wait for M2. So "performance first" means:
   - this plan's P0 runs alongside that plan now;
   - P1 onwards starts once the performance items the owner approves after M2 have landed.
4. **Script API: effect names replace the legacy paths.**
   - `Shader_Create "identity.jsl" "post/blur.jsl"` becomes `Effect_Create "PostBlur"`.
   - `Filters.lts`, and the eight scripts that name a post filter, change in the same PR.
   - The `.jsl` names leave the engine, and a mod that names one breaks.

## 1. What the code does today

### 1.1 From a name to a draw

1. **A caller names two legacy GLSL paths.** For example
   `Shader_Create("identity.jsl", "post/blur.jsl")`. There are 92 distinct pairs, reached from:
   - 93 call lines in 40 C++ files;
   - 9 glyph types' `GetShaderName` in NeuronCore;
   - 27 lines in 9 scripts.

   Every pixel shader pairs with exactly one vertex shader.
2. **The pair is looked up and cached.** `ProgramObject_Load` finds each name in two hand-kept
   tables, searched linearly (`ShaderRegistry.cpp:146-155`). It caches the pair under the string
   `vs?ps` (`Shader.cpp:216-246`).
3. **`Program::Make` reflects both blobs with `D3DReflect` at load** (`Program.cpp:65-197`). It
   finds:
   - each stage's `$Globals` by name;
   - textures and samplers by name, at t0 to t15 and s0 to s15;
   - the vertex shader's inputs;
   - the pixel shader's target count.

   Four GLSL names go through a map to their HLSL spelling (`Program.cpp:27-28`).
4. **The program keeps a CPU copy of each stage's `$Globals`.** It starts zeroed and persists
   between draws, as a GL program's uniforms did (`Program.cpp:238-242`).
5. **Setters write into that shared copy.**
   - A setter resolves its name once per program (`Shader.cpp:117-143`), writes the bytes into the
     shared copy, and makes the program "current" (`Shader.cpp:474-477`).
   - Textures are held per program, by id, in 16 "units". Each carries the sampler that its own
     texture's filter and wrap settings ask for (`Shader.cpp:202-213`).
6. **`ShaderInstance` is the material object.** It holds lazily evaluated values (`Generic<T>`) by
   uniform index, and render-state switches. `Begin` writes every value into the shared copy and
   pushes its states. Its cache for skipping unchanged state is disabled
   (`ShaderInstance.cpp:184-187`).
7. **Engine values are offered by name, to any program that has the name.**
   - `DrawState_Link` and `DrawState_Inject` (`DrawState.cpp:97-160`) offer the view matrices, eye,
     frame, time, the environment maps, and every name `DrawState_Push` supplied.
   - `Renderer_SetShader` writes WORLD, VIEW, PROJ, WORLDIT and WVP the same way
     (`Renderer.cpp:432-438`, `680-683`).
8. **A draw gathers GL-style state.** `PrepareDraw` (`Renderer.cpp:237-262`):
   - reads blend, cull and depth from push/pop stacks;
   - sets the targets;
   - binds the current program's textures and samplers (`Shader.cpp:481-510`).
9. **`DrawContext::PrepareDraw` keys, binds and uploads on every draw.**
   - It builds a `PipelineKey`: the program id, the state, eight target formats, the depth format,
     and the input layout as a string. The string is built with one `std::format` per vertex input,
     on every draw (`DrawContext.cpp:927-962`).
   - The key is looked up in a `std::map`, and a miss creates the pipeline state there and then
     (`DrawContext.cpp:1184-1221`).
   - The sampler table is 16 `D3D12_SAMPLER_DESC`, compared field by field in a `std::map`
     (`DrawContext.cpp:825-860`).
   - The texture table is copied into a descriptor ring when it changed
     (`DrawContext.cpp:866-891`).
   - Both stages' whole `$Globals` are uploaded (`DrawContext.cpp:1226-1241`).
   - The root signature has two root CBVs and two tables (`DrawContext.cpp:1569-1597`).

### 1.2 What is already standard

The core is not the GL part. `DrawContext`:
- builds real pipeline states from bytecode, state, target formats and input layout, and caches
  them;
- shares one root signature;
- tracks barriers per subresource;
- rings its uploads and descriptors;
- holds every release until its fence retires.

That is how Direct3D 12 renderers are built. "The class Program in the middle" is Direct3D 12's
pipeline state (shaders plus state) with a reflection layer above it, and engines have the same
thing as a parameter map per shader. A pipeline state that points at its shaders is already true at
the bottom. Two things are missing:
- nothing knows the pipeline states before the draw that needs them;
- everything above the core speaks GL.

### 1.3 What is GL-shaped

| ID | Problem | Where |
|---|---|---|
| G1 | Pipeline states are discovered at the first draw that needs them, and nothing makes them earlier. The migration's risk register promised known programs "warmed at load" (`Design/Archive/NeuronClient-migration.md` §10); it was not built. The first frames of each app pay the driver's compiles in-frame. | `DrawContext.cpp:1184-1221` |
| G2 | The draw path hashes strings. Per draw: a `std::format` per vertex input, a `std::map` whose key holds a `std::string` and eight formats, and a compare of 16 sampler descriptions. Per UI batch and per material change, `DrawState_Link` and `DrawState_Inject` walk every pushed name and look each one up by string. | `DrawContext.cpp:927-962`, `825-860`; `WidgetRenderer.cpp:94,130`; `RenderStyles.cpp:47-60` |
| G3 | Parameters are per-program mutable state. Every instance's `Begin` writes into the one copy all users of that program share. A value an instance does not set is whatever the last user left there. The port kept GL's semantics on purpose (migration §5.4); H2 below is what it costs. | `Program.cpp:238-242`, `ShaderInstance.cpp:183-201` |
| G4 | Render state lives in global stacks and in instances' switch lists, not with the shader. That is why no pipeline state can be known in advance. | `Renderer.cpp:190-232`, `ShaderInstance.cpp:95-153` |
| G5 | Names are GLSL-era strings end to end: `.jsl` paths in 88 C++ lines and 27 script lines, and a GLSL-to-HLSL name map in the core. | `Program.cpp:27-28`, the registries |
| G6 | Reflection runs at load, through `d3dcompiler_47.dll`. That pins the compiler to FXC and SM 5.1: ADR-008 forecloses DXC while it holds. `cfb52de` showed what happens: SM 6.7 made FxCompile hand the shaders to DXC, `D3DReflect` could not read DXIL, and 33 tests failed (`f884ca3`). | `Program.cpp:65-197` |

### 1.4 What the scripts need

The whole script surface that reaches shaders:

| Script function | Used by | Note |
|---|---|---|
| `Shader_Create`, then `ShaderInstance_Create` | 19 lines of `Texture/Filters.lts` | always `identity.jsl` and a `post/*` shader |
| `ShaderInstance_Set*`, `Set` | `Texture/Filters.lts` | float, int, Vec2, Vec3, Vec4, Texture2D, by name |
| `Texture2D_GenerateFromShader` | `Texture/Filters.lts` | a full-screen draw into a texture |
| `RenderPass_PostFilter "post/dither.jsl"` | 8 scripts: the apps' pass lists, `Widget/Observatory.lts`, `Widget/ModelEditor/Preview.lts` | a post pass by name; `LteRenderPasses.cpp:51-64` links the engine's values by name |
| `RenderPass_CustomFilter` | `Widget/Observatory.lts` | runs the script's own `Filters` |
| `Material_Metal`, `Material_Ice`, `Model_Add` | 8 lines: the item generators, `Widget/ModelEditor.lts`, `Object/WarpRail.lts` | a material is a `ShaderInstance` made in C++ |
| `ShaderInstance_Clone` | no caller | hung until P0.5 (H1) |
| `DrawState_Push`, `Pop`, `Clear` | no script caller | C++ pushes `fogDensity` and `camVelocity` |

Scripts never choose a vertex shader other than `identity.jsl`. The set of shaders is closed at
build time, because a mod overrides whole script files only (`Design/GameDesign.md` §4, Mods).

What the scripts need, then, is three things:
- an effect, by name;
- its parameters, by name;
- a way to run it into a texture or as a post pass.

None of that needs a name lookup at draw time.

### 1.5 Hazards found while reading

- **H1: `ShaderInstanceT::Clone` never advances its loop** (`ShaderInstance.cpp:171-176`). An
  instance with any render-state switch loops forever and allocates as it goes. Scripts can reach
  it through `Clone`; nothing calls it today. P0.5 fixes it.
- **H2: `prepass` is a program constant that two render styles toggle.**
  - The depth-prepass style sets it to 1 (`DepthPrepass.cpp:52-67`), and clears its cached
    instance at the start of every pass (`DepthPrepass.cpp:30-36`).
  - The G-buffer style sets it to 0 only when its cached instance changes
    (`RenderStyles.cpp:47-57`), and never clears its cache.

  Suppose a frame's first G-buffer draw uses the same material instance as the previous frame's
  last G-buffer draw, as in a scene with one material. That draw, and those right after it with the
  same instance, keep this frame's `prepass = 1` and write linear depth into the G-buffer.
  Instances are shared: every asteroid draws with `Material_Rock`'s one instance. The starfield and
  the dust flecks each system draws are additive, and the G-buffer style passes over them without
  touching its cache. Found by reading; not reproduced. P0.6 clears the cache.
- **H3: unreachable material (ADR-013).** P0.4 removed all three.
  - `MaterialLodfadePS` is registered, and nothing reaches it.
  - `DepthPrepassStyle` makes `DepthprepassPS` and an instance of it (`DepthPrepass.cpp:18-27`),
    and never draws with either.
  - `BindInput`, `BindOutput`, `Relink`, `PrintLogs` and `Create` are no-ops kept from GL
    (`Shader.cpp:258-259`, `280-283`, `293-298`). Their callers are in `WidgetRenderer`, `Font`
    and `Materials`.
- **H4: `DrawState` maps `normalBuffer` to the depth buffer** (`DrawState.cpp:99`). Both lighting
  passes overwrite it right after linking (`GlobalLighting.cpp:29-30`, `LocalLighting.cpp:103`), so
  it is harmless today because of call order. Any design that fills engine values at draw time,
  instead of at link time, would light the scene from the depth buffer. P4a keeps link-time order,
  and P4c makes an explicit value win over an engine value.

### 1.6 Numbers

Counted from the tree at `f884ca3`, by script:

| What | Count |
|---|---|
| Entry-point shaders, and includes | 113 and 17 |
| Graphics pairs | 92 |
| Compute shaders, beside the present pass | 4: the three SDF passes and mip generation |
| Loose parameter declarations | 237, with 122 distinct names; at most 9 in one entry file |
| Texture declarations | 109, with 48 names |
| Files that touch resources outside `Common.hlsli`'s `TEXTURE*` macros | `Field.hlsli`, `GenFieldCS`, `GenFieldcopyCS`, `GenFieldocclusionCS`, `GenerateMipsCS`, `PresentPS`, `Smaa.hlsli` |
| GLSL-renamed words that are parameters | `texture` alone: 29 shaders, 15 C++ lines, `Filters.lts` |
| Draws in the first PIX capture of `war` | 334 (`ShaderPerformance-plan.md`, M2) |

## 2. Where the premise is weakest

1. **"Not modern" is aimed at the wrong layer.** The core is already standard Direct3D 12 (§1.2).
   Rewriting it buys little. The value is above it:
   - pipeline states known before the draw;
   - parameters as data instead of GL state;
   - no strings on the draw path.
2. **This is not the first frame-rate lever.** At 334 draws, the string work of G2 is on the order
   of 1 to 3 ms of CPU a frame. That is estimated as 5 to 10 µs per draw, from the operations in
   §1.1 step 9, times 334; it is unmeasured. It matters only if M2 shows the frame is CPU-bound.
   The strong reasons for this plan are:
   - no in-frame pipeline compiles (G1);
   - no shared mutable parameters (G3, H2, H4);
   - a compiler switch that is a build change, not a redesign (G6).
3. **Bindless SM 6.6 solves a problem this renderer barely has, and costs the most floor.** A
   program binds at most 16 textures, and its tables are reused when unchanged. What bindless buys
   here is structural. A texture becomes two indices in the parameter block, so textures, samplers
   and constants become one kind of data: set by name from a script and uploaded as bytes. That
   deletes the ring, the sampler-table map, the per-program texture units and the per-slot binding.
   It is bought with:
   - **Windows 11, or the Agility SDK on Windows 10.** Microsoft's `D3D_SHADER_MODEL` reference
     says SM 6.6 shipped in Windows 11 and in the DirectX 12 Agility SDK. Windows 10's support ended
     in October 2025, but the README and ADR-007 promise Windows 10 today (O1).
   - **A GPU and driver with SM 6.6 and directly indexed heaps.** As far as this review recalls, the
     SM 6.6 specification ties directly indexed heaps to resource binding tier 3. The specification
     site was blocked from this session, so P0.1 settles it on each device. Tier 3 alone already
     means feature level 11_1 or above (Microsoft's hardware-tiers table), so the 11_0 floor goes
     either way.
   - **The ARM64 device (ADR-006) and WARP**, which runs every test, CI, and `--warp`. If either
     lacks SM 6.6 with directly indexed heaps, the choice breaks a target. That is P0.1's gate
     (O2, O3).
   - **The loss of the runtime's type check.** A 2D view read as a cube is undefined behaviour, not
     a validation error. The binding layer checks kinds itself instead (§3.6).
   - **The loss of bit-exactness.** DXC is not bit-exact with FXC, so generated content (noise, SDF
     fields, AO, planets, nebulae) may change in its last bits or more. The compiler switch resets
     every baseline once. It should share that window with the performance plan's D2, the new hash,
     which resets them anyway.
4. **The rename breaks mods that name `.jsl` files.** The design has the first release push no
   content (`GameDesign.md` §13.8), so the cost is low. P1 does it in one PR, so it happens once.

## 3. Target design

### 3.1 Effects

An effect is one vertex shader and one pixel shader, or one compute shader. It is declared once, in
C++, and named after its pixel or compute shader's file without the stage: `PostBlurPS.hlsl` gives
`PostBlur`, and `GenFieldCS.hlsl` gives `GenField`. Others are `MaterialMetal`, `UiPanel` and
`Smaa1`. Every pixel shader pairs with exactly one vertex shader today, so the 92 names are unique,
and callers no longer carry the pairing.

- Declarations:
  - `NeuronClient/Effects.cpp` declares the engine's effects.
  - `FrontierOutpost/GameEffects.cpp` declares the game's, replacing `ShaderRegistry.cpp` and
    `GameShaderRegistry.cpp`. ADR-016 decision 5 keeps its shape: the game's shaders live with the
    game.
- Callers name an effect by string once, when they make it, and cache it as they do today.
  - In C++, `Effect_Get(name)` returns the effect. Until P4c that is the legacy `Shader`.
  - In scripts, `Effect_Create "name"` returns a new instance of it.
  - NeuronCore's glyph types keep strings, because NeuronCore cannot see NeuronClient (ADR-014).
- `Build/CheckProjectFiles.py` checks that every effect name written as a literal names a declared
  effect. That covers `Effect_Get` in C++, `Effect_Create` and `RenderPass_PostFilter` in scripts,
  and the glyph types' names. Today only a run finds a wrong name.

### 3.2 Pipeline states before first use

- **The key becomes four interned ids**, one 64-bit value in one hash map, with no strings and no
  `std::format` on the draw path:
  - the effect;
  - the state: blend, cull, depth test, depth write, wireframe, and the depth function that the
    performance plan's E9(g) and L5 need;
  - the target formats: colour and depth, as one interned tuple;
  - the input layout, interned once per `VertexLayout`. `VertexLayout` gains a name, and the
    per-type layouts of `Renderer_DrawVertices` are interned per `Type`.
- **A manifest lists the pipeline states the apps use, by name**, as (effect, state, formats,
  layout). It is recorded, not written by hand:
  - every pipeline state made is logged with its key;
  - the fifteen apps' smoke runs collect those lines;
  - a script in `Tools/` writes the manifest as a C++ table in FrontierOutpost, compiled in, so it
    is not a runtime file (R13).

  Shipped Direct3D 12 titles precache the same way: record, then replay. It also needs no render
  state to move into the effects (§8).
- **At startup, before the app's first frame, a worker pool makes every manifest entry in
  parallel.** Pipeline-state creation on `ID3D12Device` is free-threaded. A draw whose state is
  still being made waits for it. A state missing from the manifest is made on demand and logged, so
  a gap costs a hitch, never a failure.
- **`ID3D12PipelineLibrary` is not in the plan.** A driver that reports
  `D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE` already keeps compiled states across runs, and a
  library would be a runtime file (R13). P0.2 measures whether first-run compiles still matter.

### 3.3 Reflection at build time

A build step reads each compiled blob. It writes a layout per stage into `CompiledShaders/`, which
is build output:
- every `$Globals` parameter, with its name, offset, size, type and array count;
- the resources it binds, until P6;
- the vertex shader's inputs, and the pixel shader's target count;
- a hash of the blob.

With it:
- `Program::Reflect`, and the game's run-time use of `d3dcompiler_47.dll`, go. ADR-008 decision 9's
  fallback becomes the rule.
- Until P5, the step reflects DXBC with `D3DReflect`. From P5, it reflects DXIL with
  `IDxcUtils::CreateReflection`, from the Windows SDK's `dxcompiler.dll` at build time. Nothing new
  ships.
- While FXC compiles, a NeuronClientTests case compares each generated layout with a run-time
  reflection of the same blob. From P5, DXIL cannot be reflected at run time without shipping
  `dxcompiler.dll`. The case then checks the test project's shaders against layouts written out by
  hand.
- At startup, each layout's hash is checked against its blob. A stale layout is fatal, instead of
  silently writing to the wrong offsets.

How the step is built is O4.

### 3.4 Parameter blocks and engine values

- **A `ParameterBlock` holds one stage's `$Globals` bytes, with its generated layout.** A name is
  resolved to a handle once, and setting is typed. Two cases are reported once per (effect, name),
  with the effect's parameter list:
  - a name the effect lacks, which today is a warning;
  - a value of the wrong type, which today is checked by size only.
- **An `EffectInstance`, today's `ShaderInstance`, owns its blocks.** Its lazily evaluated values
  are evaluated into them at bind. The effect's own block, today's program copy, stays for the
  callers that set parameters on the effect itself: the generators.
- **Engine values become a list resolved once per effect stage:** the parameter offsets for the
  names the engine supplies. Those are WORLD, VIEW, PROJ, WORLDIT, WVP, INVVIEW, INVPROJ, eye,
  camUp, frame, rcpFrame, time, envMap, envMapLF, depthBuffer, and each name `DrawState_Push`
  supplies.
  - Linking keeps its call sites and its order (P4a), so the port is exact; only the string lookups
    go. H4 is why the order must be kept.
  - `normalBuffer` leaves the list, since every reader overwrites it.
- **The semantic change comes last (P4c).** An instance's values stop leaking into the effect's
  shared copy, and an explicit value always wins over an engine value.
  - Before P4c lands, a Debug detector (P4b) runs every app and reports each draw that read a value
    another instance left behind.
  - `prepass` becomes a value of the pass, set by the render style for every draw. That removes H2
    by construction.

### 3.5 DXC and Shader Model 6.6

- **The FxCompile items move to Shader Model 6.6**, the same in Debug and Release (AGENTS.md §3).
  The checker's `SHADER_MODEL` follows.
- **DXC takes over through MSBuild's FxCompile**, which hands SM 6 items to it, as `cfb52de`
  showed. DXC signs its output with the SDK's `dxil.dll`.
- **The first switch compiles with `-HV 2018`**, so HLSL 2021's changes to logical and conditional
  operators on vectors do not land in the same step. Moving to 2021 is P7.
- **PIX gains source-level shader debugging** from `-Zi`, with PDBs written beside the build output.
  The setting is the same in both configurations.

### 3.6 Bindless binding

- **Root signatures are written in HLSL**, in one `RootSignatures.hlsli`, and embedded in every
  entry point with `[RootSignature(...)]`. The binding contract then sits beside the shaders, not in
  `DrawContext::Initialize`.
  - Graphics: `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
    SAMPLER_HEAP_DIRECTLY_INDEXED`, and two root CBVs: the vertex and pixel `$Globals`.
  - Compute: the two heap flags, and one root CBV.
  - No tables.
- **Every texture gets its view in the shader-visible heap when it is made**, at an index it keeps
  for its life. The index is freed through the deferred releases.
  - Views of single mips, for mip generation and compute writes, are made on first use and kept with
    the texture.
  - Null views sit at fixed indices, one per kind: 2D, 3D, cube, and each UAV kind. An unset
    parameter points at the null view of its kind.
- **Samplers are interned.** Each distinct `D3D12_SAMPLER_DESC` gets an index in the 2,048-entry
  sampler heap, once. A texture keeps the index its filter and wrap settings ask for, as GL kept the
  state with the texture, so ADR-007 decision 5 stands. The SDF field's red border is a sampler like
  any other; ADR-007's objection to static samplers no longer applies.
- **In HLSL, the macros carry the change.** `TEXTURE2D(name)` declares a `Texture2DRef`, a view
  index and a sampler index, in `$Globals`. `texture2D(name, p)` samples through
  `ResourceDescriptorHeap` and `SamplerDescriptorHeap`, and the other macros follow.
  - Of the 113 shaders, only the files §1.6 lists need edits by hand.
  - The indices are uniform across a draw, so no `NonUniformResourceIndex` is needed.
- **The generated layout knows each `*Ref` parameter's kind.** Two things follow:
  - Setting a texture checks its dimension and format class. Direct3D 12 made that check for
    tables, and cannot make it for heap indices.
  - A draw collects the textures it reads from its blocks, so the barriers and the "reads what it
    draws into" refusal keep working as today.
- **What goes:**
  - the per-draw descriptor ring and its tables;
  - `SamplerTableFor` and its map;
  - the 16 texture units per program;
  - `Shader_BindActive`'s per-slot binding;
  - the CPU pool for shader views.

### 3.7 The script API after the rename

| Today | After P1 |
|---|---|
| `ShaderInstance_Create (Shader_Create "identity.jsl" "post/blur.jsl")` | `Effect_Create "PostBlur"`, which returns an `EffectInstance` |
| `ShaderInstance_Set*`, `Set` | `EffectInstance_Set*`, `Set` |
| `ShaderInstance_Clone`, `Clone` | `EffectInstance_Clone`, `Clone` (O7) |
| `Texture2D_GenerateFromShader`, `GenerateFromShader` | `Texture2D_GenerateFromEffect`, `GenerateFromEffect` |
| `RenderPass_PostFilter "post/dither.jsl"` | `RenderPass_PostFilter "PostDither"` |
| the parameter `"texture"` | `"image"` (O6) |
| `Material_Metal`, `Material_Ice`, `Model_Add`, `DrawState_Push`, `Pop`, `Clear` | unchanged; a material is an `EffectInstance` |

P1 renames names and functions, not C++ types. `ShaderT` and `ShaderInstanceT` keep their names
until P4c rewrites them as `Effect` and `EffectInstance`, so P1 does not rename types across 40
files.

### 3.8 What does not change

- OpenGL's conventions: the clip-space macro, and rows stored bottom-up (ADR-007 decision 4).
- The shared `Varyings` struct. The performance plan put its cost under 1% of the frame.
- The push/pop render-state API. Only how its state is keyed changes.
- Uploads, barriers, deferred releases, readbacks, and PIX events.
- The generators' segmented draws.
- Bytecode compiled into the executable.

## 4. Items

Effort is S, M or L, as in the performance plan. From P1 on, every item lands as its own PR;
P0.1 to P0.6 went together, as the Status says.

| ID | Change | Output | Effort | Needs |
|---|---|---|---|---|
| P0.1 | Device probe: highest shader model, binding tier, shader-cache support, directly indexed heaps | unchanged | S | — |
| P0.2 | Pipeline-state telemetry: count, time, and a log line for each state made, with its frame | unchanged | S | — |
| P0.3 | Draw-path timing behind a define, as M1's glyph line is | unchanged | S | — |
| P0.4 | Remove H3's unreachable material | bit-exact | S | — |
| P0.5 | Fix H1: advance `Clone`'s loop | bit-exact | S | — |
| P0.6 | Reproduce H2, then clear the G-buffer style's cache in `OnBegin` | changes only where H2 shows | S | — |
| P1a | Effects replace the registries and the `.jsl` names, and the script API takes §3.7's names | bit-exact | M | the performance items approved after M2 |
| P1b | Interned pipeline keys | bit-exact | S-M | P1a |
| P2a | Record the pipeline manifest from the smoke runs | unchanged | M | P1b |
| P2b | Make the manifest in parallel at startup; wait on use; log what is late | bit-exact | M | P2a |
| P3 | Generate shader layouts at build time; remove run-time reflection | bit-exact | M-L | P1a, O4 |
| P4a | Handles and engine-value lists replace the string lookups | bit-exact | M | P3 |
| P4b | Debug detector for values one instance leaves for another | unchanged | S-M | P4a |
| P4c | Per-instance parameter blocks; `prepass` a value of the pass | bit-exact where P4b is clean | L | P4b |
| P5 | DXC at SM 6.6, `-HV 2018`, `-Zi`; the layouts reflect DXIL | visually equivalent; new baselines | M-L | P0.1, O1 to O3, P3 |
| P6 | Bindless: views and samplers in the heaps, root signatures in HLSL, `*Ref` parameters, kind checks | bit-exact against P5 | L | P5, P4c |
| P7 | Later, and only on a measured need: HLSL 2021; view and draw constants as their own cbuffers; `Varyings` per effect; enhanced barriers | per item | — | approval per item |

### P0. Probe, measure, clean (alongside the performance plan)

- **P0.1: device probe.**
  - At creation, the device logs:
    - its highest shader model, from `D3D12_FEATURE_SHADER_MODEL`;
    - its `ResourceBindingTier`;
    - its `D3D12_FEATURE_SHADER_CACHE` flags;
    - whether a root signature with both `*_HEAP_DIRECTLY_INDEXED` flags can be made.
  - A NeuronClientTests case logs the same for WARP, locally and in CI. Where SM 6.6 is missing,
    the case reports it and does not fail, since nothing needs SM 6.6 before P5.
  - The owner runs any app on the Iris Xe and on the ARM64 device.
  - **Gate for P5 and P6:** all four (the Iris Xe, the ARM64 device, local WARP, CI's WARP) report
    SM 6.6 or higher and make the root signature. Otherwise O2 and O3 are decided before P5.
  - **Landed:** `GraphicsDevice::Capabilities()`, which liblt logs at startup as one line
    beginning `Direct3D 12 supports shader model`, and the NeuronClientTests case
    `ReportsWhatTheDeviceSupports`, which writes WARP's answers to the test log.
  - **CI's WARP answered** on 2026-09-27, on GitHub's `windows-latest` runner (image
    `win25-vs2026`, 20260922): shader model 6.7, binding tier 3, directly indexed heaps, pipeline
    libraries, and no automatic disk cache. It passes the gate. The Iris Xe, the ARM64 device and
    local WARP are the owner's to run.
- **P0.2: pipeline-state telemetry.** It counts and times every pipeline state made, and logs each
  one with its key and the frame it was made in, so the states made after the app's first frame
  are the lines with a later frame. This gives G1's size, and later the proof of P2.
  - **Landed:** `GraphicsDevice::Desc::onNote` hears one line per state: its number, its program,
    each part of its key by name, the time it took, its frame, and the time spent on all of them so
    far. liblt writes the lines to its log. It logs every state, not only the late ones, so P2a
    needs no change to it.
- **P0.3: draw-path timing.** It measures the CPU time from a draw call to its record, summed per
  frame. It sits behind a define, as `TIME_GLYPHS` does (M1), and gives G2's size and point 2 of §2
  its number.
  - **Landed:** `TIME_DRAW_PATH` in `Renderer.cpp`, off. Defined, it logs for each frame the CPU
    time its draws spent from each draw call to its record, and how many draws there were.
- **P0.4: H3.** Remove:
  - `MaterialLodfadePS`;
  - `DepthprepassPS`, and `DepthPrepassStyle`'s unused shader and instance;
  - the GL no-ops `BindInput`, `BindOutput`, `Relink`, `PrintLogs` and `Create`, with their calls.

  This follows ADR-013 decision 1. **Landed** as listed; `CheckProjectFiles.py`'s registry rule
  passes without the two shaders.
- **P0.5: H1.** One line: `next = next->next;` in the loop. Whether the function stays is O7.
  NeuronClientTests cannot link the legacy files (ADR-015), so the fix is proved by reading and by
  one script run.
  - **Landed.** The loop was also run on its own, verbatim over `ListElement`'s members: without
    the line it never ends for a list of one cell, and with it lists of 0, 1 and 3 cells are copied
    in order. The script run is the owner's.
- **P0.6: H2.** First reproduce it in a scene with one material instance, for example one model in
  `model`, captured with `--frames` and `--capture` before and after. Then clear the G-buffer
  style's cached instance in `OnBegin`, as the prepass style does. Output changes only where the
  bug shows. P4c removes the cause.
  - **Landed without the reproduction,** which needs Windows. Whether `model` as shipped shows H2
    depends on what is in view, since the first and last G-buffer draws must share an instance. A
    scene where every G-buffer draw shares one: set the ship, station and planet blocks of
    `GenObject` in `App/model.lts` to `if false` locally, which leaves the 1024 asteroids, and run
    `FrontierOutpost.exe model --frames 30 --capture model.png` before and after this item. If H2
    is real, the asteroids come out wrong from the second frame on before it, and right after it.

### P1. Effects

- **P1a: effects and names, in one PR.**
  - The effect tables of §3.1 replace the two registries.
  - `Effect_Get(name)` replaces `Shader_Create(vs, ps)` at the C++ call sites (among the 93 lines
    of §1.1), and NeuronCore's 9 glyph names become effect names.
  - The 27 script lines and the script functions take §3.7's names.
  - `texture_` becomes `image` in 29 shaders, 15 C++ lines and `Filters.lts`, and
    `Program::HlslName` goes (O6).
  - The checker's registry rule becomes an effect rule: every pixel and compute shader is in exactly
    one effect, and every effect name written as a literal names one (§3.1).
    `Build/TestCheckers.py` gains fixtures for both.
  - Its ADR is (A) in §6.
- **P1b: interned keys (§3.2).** `DrawCalls`' tests keep passing, since they count pipeline states;
  a new case proves that a draw builds no strings.

### P2. Pipeline states before first use

- **P2a: the recording.** P0.2's log line already covers every pipeline state made and names each
  part of its key. A `Tools/` script reads the logs of all fifteen apps' `--frames` runs and writes
  the manifest table.
- **P2b: the replay.** The workers, the wait on use, and the log of late states (§3.2). Its ADR is
  (B).
  - **Proof:** P0.2's count of states made after the first frame falls to zero for every app the
    manifest covers.
  - The time to first frame is measured before and after.

### P3. Layouts at build time

- **The step and its checks** (§3.3):
  - the generated layouts, and the startup hash check;
  - the test against `D3DReflect`;
  - the removal of `Program::Reflect`.
- **Tests:** `ProgramReflection` and `ShaderReflection` are rewritten for generated layouts. The
  test project's own 16 shaders get layouts too, since the tests make programs from them.
- **ADR:** (C). With O4(a), it also amends ADR-014, since a tool project is added.

### P4. Parameter blocks

- **P4a:** engine values and setters go through handles resolved once per effect, in the order
  today's call sites apply them (H4). `normalBuffer` leaves the `DrawState` list.
- **P4b:** in Debug, each draw compares the block it uploads with the block it would upload if no
  instance wrote into the shared copy, and logs each difference once. Every app is run, and each
  finding is fixed or recorded before P4c.
- **P4c:**
  - `EffectInstance` owns its blocks, and an explicit value beats an engine value.
  - `prepass` comes from the pass.
  - `Program`'s last role goes: its bytecode and id move to the effect, and its constants to the
    blocks.
  - Its ADR is (D).

### P5. DXC at Shader Model 6.6

- **The switch:** the FxCompile items at 6.6 with `-HV 2018` and `-Zi`; the layouts from DXIL; the
  checker's shader model; O1's floor in the README.
  - The test project's 16 shaders move with the game's.
  - `TreatWarningAsError` makes DXC's warnings errors. They are fixed in the shaders, not silenced
    (AGENTS.md §4).
- **The same window as the performance plan's D2**, so content baselines reset once.
- **Proof:**
  - the image diff on all fifteen apps (maximum and mean per channel, pixels over the threshold) and
    a side-by-side look;
  - M1's generation lines re-measured;
  - the debug layer clean, and every state made.
- **ADR:** (E).

### P6. Bindless

§3.6 in full.

- **ADR:** (F). It amends ADR-009 decision 3 together with the performance plan's D1, whose static
  sampler becomes a heap sampler here.
- **Tests:** `TexturedDraws`, `ComputeDispatches`, `MipGeneration` and `DrawCalls` move to
  parameters. New cases cover null views and the refusal of a wrong kind.
- **Proof:** images identical to P5's. GPU-based validation is run once per app on the owner's GPU.

## 5. Decisions for the owner

| ID | Decision | Recommendation |
|---|---|---|
| O1 | Windows floor for SM 6.6: Windows 11, or Windows 10 with the Agility SDK. The Agility SDK is a NuGet package (R14), and puts `D3D12Core.dll` beside the executable (R13). | **Windows 11.** No new dependency, and Windows 10's support ended in October 2025. The README, ADR-007 and ADR-008 change with P5; ADR-003's mention of Windows 10 stays true. |
| O2 | If a GPU target fails P0.1: drop that target, or keep SM 6.6 without bindless (tables stay, and §3.6 does not land). | Decide on P0.1's results. **Do not build two binding paths.** |
| O3 | If WARP fails P0.1: WARP's NuGet package for the tests (a dependency; whether R14 binds a test-only package is itself the owner's call), or a test runner with a GPU. | Decide on P0.1's results. |
| O4 | How the layouts are generated. **(a)** A small C++ console project, built for the build host, that reflects through the SDK's `D3DReflect` and `dxcompiler.dll`. **(b)** A Python script that parses DXC's `-Fc` listing. **(c)** Generated layouts committed, with CI checking they are fresh. | **(a).** It uses the reflection API DXC defines, not its disassembly text, and the build still needs Visual Studio alone. It costs a project in the solution, an ADR-014 amendment, and a host-platform mapping for ARM64 builds. (b) makes Python a build prerequisite, and parses text DXC does not promise to keep. (c) keeps a second source of truth, held only by a check. |
| O5 | The manifest: recorded and compiled in, or state declared per effect. | **Recorded** (§3.2, §8). |
| O6 | `texture` becomes `image` in P1, or the GLSL name map stays. | **Rename.** The scripts change in P1 anyway. |
| O7 | `EffectInstance_Clone`: kept and fixed, or removed with its code. ADR-013 decision 8 allows removal, since it is the only way into code of its own. | **Keep.** After P4c, a clone is a copy of the blocks and costs nothing to maintain, and a script making variants of a filter is a natural use. |
| O8 | Errors in parameters (§3.4): a warning once, or fatal like an unknown effect. | **A warning once** on the client. The server's fail-fast rule (`GameDesign.md` §13.8) does not reach rendering. |

## 6. ADRs this plan writes

| | ADR | Supersedes or amends |
|---|---|---|
| (A) | Effects replace the legacy shader names | ADR-008 decisions 4 and 5, and decision 2 for new files; ADR-016 decision 5; README |
| (B) | Pipeline states are made before first use, from a recorded manifest | ADR-007 decision 2 ("created at first use") |
| (C) | Shader layouts are generated at build time | ADR-008 decisions 4 and 9; ADR-014 with O4(a) |
| (D) | Parameters belong to effect instances | the GL persistence that ADR-007 and migration §5.4 kept |
| (E) | Shaders are HLSL, compiled by DXC at Shader Model 6.6 | ADR-008 decisions 1 and 3, and its "forecloses DXC" clause; ADR-007 decision 1; with O1 = Agility SDK, a dependency ADR as well (R14) |
| (F) | Resources are bound through directly indexed heaps | ADR-007 decision 2's root-signature, descriptor and sampler policies; ADR-009 decision 3, with the performance plan's D1 |

## 7. Order of work and verification

### Order

1. **Now, alongside the performance plan:** P0.1 to P0.6. They touch no API that the performance
   items use.
2. **After the performance items approved after M2 have landed:** P1, P2, P3 and P4, in that
   order. Each is bit-exact against the FXC baseline, which is why they all come before the compiler
   switch.
3. **P5, in one window with the performance plan's D2**, so every generated baseline resets once.
4. **P6.**
5. **P7 items**, only on measurements.

### Where the two plans meet

| Performance item | Meets | Resolution |
|---|---|---|
| E1, E2(b), E3(a), E5(3), E7 | constants set by name through today's setters | they land first; P1 and P4 carry them, with names becoming handles |
| E9(g), L5 | the depth function in the pipeline key | P1b's state id includes it |
| D1 | a static sampler in the compute root signature | it lands first, in C++; P6 moves it into the heap |
| D2 | a new hash, so new content | it shares P5's baseline window |
| D3 | disk caches | independent (R13) |
| D4, D5, D7, D8, D9 | content and formats | independent of the binding |

### Verification of every item

- **Build** Debug|x64 and Release|x64 through the solution, and ARM64 when C++ changes (ADR-006).
- **Checkers:** `CheckFormat.py`, `CheckProjectFiles.py` (with P1a's and P5's new rules),
  `RunClangTidy.py`, `TestCheckers.py`.
- **Tests:** NeuronClientTests on WARP, with the debug layer.
- **Bit-exact items:** identical `--frames N --capture` images on the owner's GPU, with `srand`
  pinned as the perf-review skill's §3 describes, for all fifteen apps.
- **Visually equivalent items:** that skill's image diff, and a side-by-side look.
- **Anything that renders:** `war` and `ltheory` run interactively (AGENTS.md §3).

## 8. Checked and dismissed

- **Shipping compiled shaders as files.** Answered in §0: the bytecode stays in the executable.
- **Declaring render state per effect, instead of recording a manifest.** It would move state out
  of the stacks at every legacy draw site, for nothing the manifest does not already give. Revisit
  if the manifest misses too much.
- **Converting to Direct3D's top-left conventions.** ADR-007 decision 4; it is independent of the
  binding.
- **Rewriting shader bodies.** The GLSL transliteration stays; only declarations change.
- **Per-effect `Varyings`.** Under 1% of the frame (the performance plan's §5).
- **A pipeline library** (§3.2).
- **Splitting view and draw constants into their own cbuffers now.** At 334 draws, uploading each
  stage's whole `$Globals` is about 170 KB a frame (estimated at 256 bytes a stage, two stages,
  per draw). The split would touch every shader that declares a matrix. It is P7, on a measured
  need.
- **Two binding paths**, tables and bindless, for devices that fail P0.1 (O2).
- **Root constants for WORLD, WORLDIT and WVP.** They are 48 of the 64 DWORDs a root signature
  holds; a root CBV is simpler.

## 9. Evidence, and what is not verified

**Checked by reading, at `f884ca3`:**
- every flow in §1, at the lines cited;
- the counts in §1.6, by scripts over the tree: the pairs from every `Shader_Create` and
  `ShaderInstance_Create` literal, the glyph names, the post-filter names and `ExplosionVisual`'s
  two; the declarations by a parser of the HLSL's global declarations;
- that `cfb52de`'s SM 6.7 items went to DXC and that `D3DReflect` failed on them, from `f884ca3`'s
  message.

**Confirmed in Microsoft's documentation (2026-09-27):**
- FXC compiles root signatures written in HLSL, for SM 5.0 and up, as `rootsig_1_0` and
  `rootsig_1_1`.
- `ID3D12PipelineLibrary` needs `D3D12_SHADER_CACHE_SUPPORT_LIBRARY`, and
  `..._AUTOMATIC_DISK_CACHE` means an OS-managed disk cache across runs.
- SM 6.6 shipped in Windows 11 and in the Agility SDK.
- Resource binding tier 3 means feature level 11_1 or above.
- The directly indexed root-signature flags exist, for `ResourceDescriptorHeap` and
  `SamplerDescriptorHeap`.

**Not verified:**
- Which binding tier directly indexed heaps need. The SM 6.6 specification's site was blocked from
  this session; P0.1 decides.
- Whether the Iris Xe, the ARM64 device and local WARP support SM 6.6 with directly indexed heaps
  (P0.1). CI's WARP does: shader model 6.7 at binding tier 3, on 2026-09-27.
- H2 in a run (P0.6), whose fix landed without one.
- Every cost figure (P0.2, P0.3), whose instruments landed without a run.
- How far DXC's output differs from FXC's on this content (P5).
- That every shader compiles under DXC with `-HV 2018` without edits. A search found every `?:`,
  `&&` and `||` in the HLSL applied to scalars, which HLSL 2021 accepts too, so P7's move to 2021
  may be small. That is a search, not a compile.

## 10. Open for the owner

1. O1 to O8 (§5).
2. Approval of items by ID. P0.1 to P0.6 have landed, and their runs are the owner's (§4, P0);
   nothing from P1 on starts before the performance items the owner approves after M2 have landed.
