# Shader performance: generation passes, compute shaders and the frame

- **Status:** Proposed 2026-09-26. Nothing is approved and nothing is implemented. The owner
  approves items by ID. Each approved item lands as its own PR (AGENTS.md §6), with its ADR in the
  same commit where the item is a decision (§3). The next free ADR number is ADR-016.
- **Scope:** the HLSL in `NeuronClient/Shaders/` and the C++ that dispatches it:
  - the four compute shaders;
  - the pixel shaders that do compute work at load;
  - the per-frame passes.
- **Source:** a read-only review on 2026-09-26. The four compute shaders were analysed in depth. The
  other ~125 shaders were reviewed by lens (generation, scene, post and UI), and each finding's
  mechanism was checked against the code. §6 says what was measured and how; every other figure is
  an estimate, and says its method.
- **Bounds:**
  - FXC at Shader Model 5.1 and feature level 11_0 (ADR-007, ADR-008): no wave intrinsics, no
    SM 6, no 16-bit types. Nothing below needs them.
  - The equivalence bar is "visually equivalent", as `.claude/skills/perf-review/SKILL.md` §3
    defines it. An item that lowers quality, changes seeds or changes generated content is a
    decision for the owner (§3), however cheap.

## 1. Where the time goes

**Load-time generation costs far more than the frame.** The largest single cost is not one of the
four compute shaders. It is a pixel shader doing compute work: the plate-mesh occlusion bake.

| Work | When | Cost | Basis |
|---|---|---|---|
| Plate-mesh AO (`ComputeOcclusionPS`) | every load, once per hull type | ~6.7 s per system init on a GTX 1060-class GPU | estimated: ~1.1e11 vertex-triangle pairs for the system `hud.lts` builds, at a texture-bound rate |
| IR map (`CubemapIrmapPS`) | every load, once per system | ~95 ms GPU + 60-120 ms CPU and PCIe | estimated: 2.15G cube taps + 2.15G sample-buffer taps, 66 synchronous readbacks |
| SDF field (`GenFieldCS`) | once per asteroid model | 20-23 ms (67 ms at worst) on an Iris Xe; 22 s on WARP | measured (ADR-009 decision 6), Debug with the debug layer |
| SDF AO (`GenFieldocclusionCS`) | once per LOD level | not measured (M1); about 2-3× the field per model | estimated from texture fetches: 2,116 samples × 9 fetches per vertex |
| Glyph SDF (`ComputeSdffontPS`) | each glyph, at first use (a frame hitch) | 1-2 ms per glyph; 3-4 ms on an Iris Xe | estimated: 106.5M taps per glyph |
| The frame | every frame | ~3-5 ms at 1080p on a GTX 1660-class GPU; 2-3× that on an iGPU | estimated from the passes' bytes and operations |

**Three of the hottest loops share one pattern.** Every thread of a wave reads the same element,
at the same time, through the texture unit:

- the surfels (the triangles the bake sums over) in `ComputeOcclusionPS.hlsl:35-36`;
- the sample directions in `GenFieldocclusionCS.hlsl:37`;
- the sample buffer in `CubemapIrmapPS.hlsl:23`.

A texture fetch costs the same whether the lanes' addresses agree or not. The fix is the same in
each case: put the data in the constant buffer, where a uniform index becomes a scalar or
broadcast read, or stage it in groupshared memory. ADR-009 made the same argument for the SDF
instructions.

**The frame has no single dominant cost.** The frame's cost is spread over a dozen passes. The
per-frame items are worth doing, but they will not fix a low frame rate if the frame is
CPU-bound; M2 answers that.

## 2. Items that keep the output

Each is one PR. "Bit-exact" items are proved by comparing generated data on the same GPU (§4).
"Last-bits" items are proved by the image diff in the perf-review skill's §3. Every gain below is
an estimate unless §6 gives a measurement.

| ID | Change | Output | Effort | Estimated gain |
|---|---|---|---|---|
| M1 | Timing log lines for the AO bakes, the IR map and glyph SDFs | unchanged | S | makes the rest measurable |
| M2 | Baseline on the owner's GPU and on WARP | unchanged | S | — |
| E1 | `GenFieldCS` computes only its batch's slices | bit-exact | S | ~4× per field on WARP; a few % on a GPU |
| E2 | SDF AO: branchless filter, directions in the constant buffer, 64×1 groups | bit-exact | S | RDNA2: 175 → 114 instructions per sample; ends WARP's idle lanes |
| E3 | Plate AO: surfels in the constant buffer; no CPU wait per band | bit-exact in practice | S-M | texture-bound → ALU-bound: ~2-4× on the largest load cost |
| E4 | Mips made where they are needed: font atlas, cube faces, IR temporaries | bit-exact | S | 6× fewer mip dispatches for cubes; no per-glyph atlas chain |
| E5 | IR map rendered into its own mips; sample directions read once | bit-exact | M | ~30 ms GPU and 60-120 ms CPU/PCIe per system |
| E6 | Glyph SDF: pruned search and `GatherRed` | bit-exact | S-M | 3-10× fewer taps per glyph |
| E7 | Point lights: loop over the real light count | bit-exact | S | 0.2-0.5 ms per frame |
| E8 | Global lighting: branch on the material | bit-exact (last bits on ice) | S | 0.1-0.25 ms per frame |
| E9 | Seven small per-frame fixes, (a) to (g) | bit-exact | S each; M for (g) | 0.05-0.3 ms each |
| L1 | Bloom blur: 65 taps paired into 33 bilinear taps | last bits | S | ~8M fewer fetches per frame |
| L2 | Lens flares blended straight into the frame | last bits | S | 0.15-0.55 ms per frame |
| L3 | Starfield quads sorted by direction | last bits | S | unknown; PIX |
| L4 | Composite: background only where geometry is not opaque | last bits | S | part of 0.05-0.15 ms |
| L5 | The depth prepass reused for early-Z | last bits | M | 0.1-0.5 ms in cluttered fields |
| L6 | Nebula, planet and rock generators: uniform terms hoisted | last bits | S | nebula ~0.85 → ~0.5 s (cold cache only) |
| L7 | Material and planet micro-fixes | last bits | S | 0.03-0.1 ms |

### M1. Timing lines

`SDFMesh` logs how long each field took (ADR-009 decision 6), and nothing else here is timed. Add
the same line to:

- `GenerateOcclusion::OnEnd` (`SDFMesh.cpp:570`), per level, with its vertex count;
- `Mesh_ComputeOcclusion` (`PlateMesh.cpp:112`), with vertices, surfels and bands;
- `Generator_IRMap` (`IRMap.cpp:12`);
- `AddGlyph` (`Font.cpp:68`).

The AO bakes and the IR map already wait for the GPU, so their times include GPU time: the
scheduler waits after each job run, the plate bake after each band, and the IR map at each
readback. A glyph's time needs a GPU wait added in the measuring build. Whether the lines stay
committed is question 3 in §7.

### M2. Baseline

This follows the perf-review skill's §4: Release|x64 on the owner's GPU, median of three runs.

- M1's lines for `war`, `ltheory` and `hud` (the system with the boss hull);
- the time to first frame, and the time until the scheduler is idle;
- a PIX capture of one steady-state `war` frame, for per-pass GPU times, and whether the frame is
  CPU-bound or GPU-bound;
- the WARP smoke run: `FrontierOutpost.exe war --warp --frames 30`.

The ranks of E3, E5 and the per-frame items are provisional until this exists.

### E1. `GenFieldCS` computes only its batch

**The bug.** `SDFMesh.cpp:353` dispatches `Groups(slices, 4)` groups in z. `GenFieldCS.hlsl:98-100`
then tests each voxel against the whole field, not the batch. So a 1-slice batch computes 4 slices,
and the next batch recomputes 3 of them.

**Why WARP pays 4×.** The scheduler's first run of a job is always 1 unit (`Scheduler.cpp:144-147`).
At ADR-009's 22 s per 128³ field, a run takes about 170 ms, which is over the 16.7 ms budget, so
WARP's batch never grows past 1 slice. Every WARP field is computed about four times, and the 22 s
holds about 5.5 s of work.

**Change.** Add `uint sliceCount;` to `$Globals`, and return when `id.z >= sliceCount`. Round the
batch up to a multiple of 4 slices wherever the field has room, so no lane idles.

**Test.** A NeuronClientTests case dispatches the compiled `GenFieldCS` for one slice of a small
field, and checks that no other slice is written.

### E2. SDF AO: the same arithmetic, fewer instructions

Where: `Field.hlsli:42-62`, `GenFieldocclusionCS.hlsl`, and `SDFMesh.cpp:479-620`. Three
independent changes. (a) and (b) came out bit-identical on lavapipe (§6). (c) only changes which
thread computes which vertex, so it is exact by construction.

**(a) Branchless filter.** `SampleField` issues its eight `Load`s together, and puts the border's 1
in with selects instead of a branch around each load. A `Load` outside the texture returns 0 in
Direct3D, and the select replaces it. The texels, the weights and the lerp order are unchanged.
`GenFieldcopyCS` shares the function, and is bit-exact too.

```hlsl
float SampleField(Texture3D<float> source, uint3 size, float3 coord) {
  float3 texel = coord * float3(size) - 0.5;
  float3 lower = floor(texel);
  float3 t = texel - lower;
  int3 at = int3(lower);
  int3 upper = at + 1;
  bool3 in0 = at >= 0 && at < int3(size);
  bool3 in1 = upper >= 0 && upper < int3(size);
  float v000 = source.Load(int4(at.x, at.y, at.z, 0));
  // ... the seven other corners, as v100 = Load(upper.x, at.y, at.z) and so on
  v000 = (in0.x && in0.y && in0.z) ? v000 : 1.0;
  // ... each corner with its own in0/in1 per axis
  float x00 = lerp(v000, v100, t.x);
  // ... x10, x01, x11, then lerp by t.y and t.z exactly as today
}
```

**(b) Directions in the constant buffer.** They become `float4 directions[MAX_SAMPLES]` in
`$Globals`, with `#define MAX_SAMPLES 2116u`. That is 33.9 KB, under the 64 KB limit. The job sets
them with `SetConstant("directions", …)` instead of creating `noiseTexture`, and checks that
`samples <= MAX_SAMPLES`, as it checks `kMaxInstructions` today.

**(c) Contiguous vertex batches.** `[numthreads(64, 1, 1)]` over a contiguous range of vertices,
with the texel found as `(vertex % dimension, vertex / dimension)`. The job's unit becomes one row
of `dim` vertices instead of one column.
- Today, a 1-column batch leaves 56 of every 64 lanes idle. That happens on every job's first
  run on a GPU. On WARP the batch likely never grows past one column, for the reason E1 gives, so
  it happens on every run.
- Contiguous vertices from marching cubes are spatially close, so a wave reads nearby texels.

**Not in E2: the field's transform folded into one multiply-add.** It changes the last bits of the
sample positions.

**Test.** A test shader compares today's filter with the new one bit for bit, at in-range
coordinates and at border coordinates.

### E3. Plate AO: surfels from the constant buffer

Where: `ComputeOcclusionPS.hlsl:26-49` and `PlateMesh.cpp:112-201`.

**(a) Surfels in the constant buffer.** Each band's surfels go into `float4 sPoint[2040]` and
`float4 sNormal[2040]` in the pixel shader's `$Globals`, through `SetFloat4Array`
(`Shader.cpp:400`), in today's order, padding included. The loop indexes them with its counter,
and the band keeps its row boundaries.
- Big hulls have one-row bands of at most 2,040 surfels (the boss hull's sDim is about 601, by
  the reviewer's count), so they are bit-exact in practice: a sample at a texel centre returns the
  texel.
- Smaller meshes whose bands exceed 2,040 surfels split there, which changes their bands'
  summation grouping in the last bits.
- The upload page is 4 MB (`GraphicsDevice.h:34`), so a 64 KB block per band stays in the ring.

**(b) No CPU wait per band.** `PlateMesh.cpp:190` becomes `Renderer_Flush()`: each band is still
its own submission, which is what protects against a TDR, but the CPU no longer waits for it. Wait
every N bands, so that the upload ring recycles.

**Alternative.** A `cs_5_1` version that stages 64 surfels at a time in groupshared memory. It
keeps any band size exactly, but moves the accumulation from the blend unit to a UAV.

**Evidence** (§6): today each vertex-triangle pair costs 2 filtered RGBA32F samples, 2 descriptor
reloads and 27 VALU instructions. With (a) it costs 2 scalar loads and 24 VALU instructions, with
no texture traffic. The reviewer's estimate for the system `hud.lts` builds is ~6.7 s → ~2.3 s per
system init on a GTX 1060-class GPU, plus 80-260 ms from (b). The hull sizes behind that estimate
were counted from `Generate.lts`, not run: M1 settles them.

### E4. Mips made where they are needed

- **(a) The font atlas.** `Font.cpp:137` rebuilds the whole 1024² atlas's chain after every glyph:
  10 dispatches and 10 barrier batches, in the middle of a frame. Build it once per `Draw` or
  `GetTextSize` that added glyphs. Mips depend only on mip 0, so this is exact.
- **(b) Cube maps.** `DrawContext.cpp:1826-1837` issues one dispatch per face per mip, each after a
  barrier flush of its own: 60 for a 1024 cube, where 10 would do.
  - The SRV and UAV in `MakeMip` (`DrawContext.cpp:1040-1094`) become arrays of `faces` slices.
  - `GenerateMipsCS` takes its slice from `SV_DispatchThreadID.z`.
  - One barrier batch covers all six faces.
  - `MipGeneration.cpp` gains a cube case.
- **(c) IR-map temporaries.** `IRMap.cpp:60` builds a full mip chain for each temporary cube, and
  never reads it. Pass `generateMips = false`. E5 removes the temporaries altogether.

### E5. IR map rendered into its own mips

Where: `IRMap.cpp:12-73` and `CubemapIrmapPS.hlsl:16-30`. Today each level renders into a
temporary cube, reads it back one face at a time, and uploads it into the IR map. Level 0 makes the
same round trip: 192 MB.

**Change.**
1. Render level i straight into mip i. liblt's `Renderer_PushColorBuffer` (`Renderer.cpp:721`) gains
   a mip argument; `ColorTarget` already carries one (`DrawContext.h:149-154`).
2. Copy level 0 on the GPU, with a `Load` pass, since `DrawContext` has no copy.
3. Read the 1,024 sample directions once, into `float4 sampleDirections[1024]`. They are not the
   raw vectors: `u = (i + 1) / (samples + 1)` misses texel centres, so each is a bilinear blend of
   two neighbours. A 1024×1 pass that runs the shader's own `texture2DLod` expression, read back
   once (16 KB), keeps them bit-exact. Recomputing them on the CPU would not.

### E6. Glyph SDF: the same minimum from fewer taps

Where: `ComputeSdffontPS.hlsl:18-32`. Today each texel takes 129² = 16,641 taps over an 80×80
tile, which is 106.5M taps per glyph.

**Change.**
- Skip offsets with |o| > radius + 1.
- Walk rows by increasing |y|, stop once |y| ≥ d + 1, and trim each row to the remaining disc.
- Read 2×2 texels per `GatherRed` at texel corners. The bitmap and the SDF are both 1024², so the
  coordinates are exact.

**Why it is bit-exact.**
- `min` is exact and independent of order.
- Every skipped candidate is provably ≥ the result, or ≥ radius, and the +1 margin covers the
  error of `sqrt`.
- Gather returns raw texels.

**Test.** A test renders a fixed bitmap through the old shader and the new one, and compares them.

### E7. Point lights: the real light count

`LocalLighting.cpp:52-56` pads every batch to 16 lights with zero colours, and
`LightPointPS.hlsl:34-78` loops over all 16.

**Change.** Add `int lightCount`, set before the padding. The loops run to it (`[loop]`), and the
material chain gets `[branch]`.

**Why it is bit-exact.** Each skipped term is exactly +0. The reviewer checked that `cookTorrance`
stays finite: every material that uses it writes a roughness of at least 0.01.

### E8. Global lighting: branch on the material

`LightGlobalPS.hlsl:28-78` flattens all four material branches. Every pixel, sky included, pays 4
cube fetches and ~48 transcendentals, and no shader ever writes `MATERIAL_PHONG`.

**Change.**
- Branch on the material.
- Compute the reflection directions and their `ddx`/`ddy` before the branch, and use
  `textureCubeGrad` inside it. Implicit derivatives inside flow control are why the branches were
  flattened.
- Drop the clear at `GlobalLighting.cpp:32`: the full-screen pass overwrites every pixel.

**Output.** Bit-exact on NOSHADE and COOKT pixels. The last bits change on ICE pixels, where
explicit gradients replace implicit ones.

### E9. Small per-frame fixes, one PR each

- **(a)** `UiBasicPS.hlsl` and `UiNonePS.hlsl`: `discard` where the layer's alpha is 0. Alpha
  blending returns the destination there anyway. `UiBasicPS` otherwise runs ~22 transcendentals
  on every screen pixel.
- **(b)** Rendered's copy into the frame (`Rendered.cpp:133-137`): `BlendMode::Disabled`. Blending
  with alpha 1 gives the source anyway.
- **(c)** `LightCompositePS.hlsl:19-27`: `[branch] if (fog > 0)` around the fog colour. Fog is
  exactly 0 away from asteroid fields.
- **(d)** `PostBlurPS.hlsl:23-24`: `SampleLevel(…, 0)` instead of `SampleGrad`.
  - Bit-exact for bloom, whose sampler has MaxLOD 0 and linear min and mag.
  - Gated by a uniform that `Bloom.cpp` sets, because `Filters.lts` runs the same shader with other
    samplers.
- **(e)** Lens flares: skip the clear and the composite when no flare was drawn. The output is
  exactly the input then.
- **(f)** Per-draw constants that are computed per pixel move to the vertex shader, as
  `nointerpolation` varyings: `StarbgPS.hlsl:19`, `LensflarePS.hlsl:18`, `LightGlobalPS.hlsl:16`,
  `ExplosionPS.hlsl:27-29`, `ShieldExplosionPS.hlsl:31-35`, `TransferbeamPS.hlsl:22` and
  `WormholePS.hlsl:18`.
- **(g)** SMAA's weight pass (preset ULTRA, `Smaa.hlsli:244`) runs on every pixel.
  - A depth mask written where the edge pass does not discard lets early-Z cull non-edge tiles.
    `Smaa.hlsli:131-134` recommends it.
  - It needs a depth function in the pipeline-state key; `DrawContext.cpp:212` fixes LESS today.
  - The weights clear at `SMAA.cpp:76` is redundant today, but not once the mask exists.

### L1 to L7. Last-bits items

- **L1** Bloom: pair taps i and i+1 into one bilinear tap at `i + w(i+1) / (w(i) + w(i+1))`,
  weighted `w(i) + w(i+1)`. That is 65 → 33 taps, over two passes at quarter resolution.
- **L2** Lens flares: `LensflarePS` multiplies by `1 + 0.5 · dirt` and blends additively into
  primary. The full-screen clear, the composite and the Flip go (`LensFlares.cpp:97-170`).
- **L3** Starfield: sort the ~100k star quads by cube face and Morton order when the mesh is built
  (`Starfield.cpp:40-62`). About 6.4 of them are blended over every sky pixel, in random order
  today.
- **L4** `LightCompositePS`: `[branch] if (albedo.w < 1)` around the background, with
  `textureCubeGrad`. `SkyboxPS` writes alpha 1 under all geometry.
- **L5** Early-Z:
  - keep the prepass depth (`DepthPrepass.cpp:113` clears it);
  - draw the G-buffer with LESS_EQUAL and depth writes off;
  - drop the `EARLY_Z` discard (`Common.hlsli:219`);
  - mark `NpmVS`'s position math `precise`;
  - draw the System interior (the skybox) last.

  It shares E9(g)'s pipeline-state key change.
- **L6** Hoist the terms that depend only on the sample index and seed out of the per-texel loops
  of `GenNebulaPS` (`:29-39`, `:75-88`), `GenPlanetPS` (`:21-27`, `:47-48`) and `GenRockPS`
  (`:20-26`). These run on a cold cache only. Check with `fxc /Fc` first whether FXC already
  hoists them.
- **L7** Small fixes:
  - drop Metal's bump, which enters the normal at weight 1e-4 (`MaterialMetalPS.hlsl:51-53`);
  - drop `length()` of an already-normalised vector (`PlanetPS.hlsl:46`);
  - count the scattering loop with an int (`Scattering.hlsli:54`).

## 3. Decisions for the owner

Each of these crosses an ADR, adds a runtime file (R13), or changes what is seen or generated. If
approved, each gets an ADR in the same commit.

| ID | Decision | What it buys | What it costs or forecloses |
|---|---|---|---|
| D1 | A static sampler in the compute root signature for the SDF field: trilinear, border, `D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE`. `GenFieldocclusionCS` and `GenFieldcopyCS` then filter in hardware. | With E2(b), RDNA2: 175 → 32 instructions and 9 → 1 memory ops per AO sample; 3.8× on lavapipe (measured, §6); 3-5× expected on a GPU. | Amends ADR-009 decision 3 ("compute is given no samplers"). ADR-007 says static samplers "would not do: the SDF field's border colour is red", but the field is R32F, so only red is read, and opaque white's red is 1.0. Hardware filters with ≥ 8-bit sub-texel weights: emulated, AO moves by ≤ 8.2e-4 (§6), and results then differ between GPUs in the last bits. |
| D2 | Replace the sin-based hash in `Noise.hlsli` with a float-only hash (Hoskins' hash-without-sine), not PCG3D. The scope is either the SDF path's Worley noise or every noise user. | RDNA2, per octave of `GenFieldCS`: 689 → 664 VALU instructions and 67 → 0 `v_sin`: ~25% less issue time, more on NVIDIA. Shapes become reproducible across GPUs: D3D bounds `sin` only within ±100π, while arguments reach ~1,000 (the seed), and ~2,048 in planets, and are multiplied by 4137 before `frac`. | New asteroid, planet and nebula shapes for the same seeds. PCG3D is ~50% worse on RDNA2 (153 quarter-rate integer multiplies per octave), so measure any hash per vendor. |
| D3 | Disk caches for the plate AO (keyed by a hash of positions and indices; 0.9 MB for the boss hull) and for the IR map's mips 1-10 (~32 MB per system). | E3's and E5's costs vanish on a warm load; the seeds are fixed. | Two runtime files, under R13, in ADR-004's cache class. |
| D4 | The SDF AO sample pattern: 2,116 samples today (`SDFMesh.cpp:526`). Ring 0 has radius 0, so its 46 samples all point along the normal, and j = 0 and j = 45 give the same angle in every ring (`:550-559`). A stratified hemisphere of ~512 samples with the same exponential radii. | ~4× less AO work. | Different AO values. |
| D5 | Plate AO: an exact near field and a clustered far field (hierarchical). | The algorithmic fix for an all-pairs sum. | Different AO values. |
| D6 | Pass merges: global lighting, point lights and the composite into one pass (~86 MB per frame less); bloom composite, tonemap and colour grade into one (~66 MB); the HUD layer straight onto primary in `war`. | Bandwidth: ~0.2-0.6 ms per frame. | The render-pass list changes. |
| D7 | SMAA at preset HIGH, and 8-bit edge and weight targets. | Less search work and bandwidth. | Visible; GL used ULTRA. |
| D8 | RGBA16F instead of RGBA32F for the environment cubes (nebula, blur, IR map). | Halves ~190 MB of VRAM, the cache files and the uploads; about twice the tap rate for the blur and the IR map. | A 10-bit mantissa. |
| D9 | SDF fields evaluated in a narrow band: a coarse pass with the field's Lipschitz bound skips the noise where the surface cannot be. | For asteroids, about two thirds of the 128³ voxels skip the noise (estimated). | Values far from the surface change: invisible, but needs proof. Effort M-L. |
| D10 | Per-frame options that change the image: starfield baked or with smaller quads; less anisotropy on triplanar textures; a point-light cutoff or light volumes; planet scattering at half resolution. | 0.2-1 ms each, depending on the scene. | Visible. |

## 4. Order of work and verification

**Order.**
1. M1 and M2, before anything else.
2. E1, the smallest change, which also speeds up the WARP smoke runs.
3. E3, the largest estimated cost.
4. E2, and D1 after it if approved (D1 builds on E2's structure).
5. E4: (c) lands only if E5 is not approved.
6. E5.
7. E6.
8. E7, E8, then E9 (a) to (g). E9(g) lands the pipeline-state key change that L5 reuses.
9. L1 to L7, once the owner has seen the numbers from the steps above.
10. The D items, as approved. D2 and D3 do not depend on the rest.

**For every item:**
- **Build** Debug|x64 and Release|x64 through the solution, and ARM64 when NeuronClient C++
  changes (ADR-006).
- **Checkers:** `CheckFormat.py`, `CheckProjectFiles.py` (a new shader or test file is registered in
  the `.vcxproj`, the `.filters` and the registry), and `RunClangTidy.py`.
- **Tests:** NeuronClientTests, on WARP with the debug layer, including the item's new test. liblt
  code gets no NeuronClientTests (R9), so E3 and E5 are proved by the comparisons below.
- **Bit-exact items:** on the same GPU before and after, a local edit prints a hash of every buffer
  the item generates (field, LOD grids, per-vertex AO, IR-map mips, glyph atlas), and the hashes
  match. `FrontierOutpost.exe <app> --frames N --capture <png>` gives identical images, with `srand`
  pinned as the perf-review skill's §3 describes.
- **Last-bits items:** that skill's image diff (maximum and mean per channel, pixels over a
  threshold), and a side-by-side look.
- **Timing:** M1's lines, median of three runs, Release|x64 on the owner's GPU. The per-frame items
  are timed with PIX per pass; E1 and E2 are also timed on the WARP smoke run.
- **Looked at:** `war` and `ltheory` run interactively for anything that renders (AGENTS.md §3).

## 5. Checked and dismissed

- **The SDF interpreter's overhead.** For an asteroid the program is two instructions
  (`RenderableAsteroid.cpp:17-18`). Its stacks compile to registers without scratch (80 VGPRs on
  RDNA2), and its branches are uniform. The noise is the whole cost.
- **Hoisting the sin hash by hand in `GenFieldCS`.** The compiler already shares it across the 27
  cells: 3 + 9 + 27 + 27 + 1 = 67 `v_sin` per octave, as §6 counts.
- **`GenFieldcopyCS` on its own.** It runs once per LOD grid; it gains only as part of D1 (181 → 39
  instructions).
- **`GenerateMipsCS`'s arithmetic** (80 instructions, 4 loads). Only its callers are at fault (E4).
- **`ComputeLensflareVisibilityPS`.** It covers 64 pixels, read back asynchronously.
- **The present pass, the full-screen quad's diagonal, and the shared 22-register varyings.** Each
  costs well under 1% of the frame.
- **Shader Model 6 and wave intrinsics.** Nothing above needs them, and ADR-008 forecloses DXC while
  reflection runs at load.

## 6. Evidence

**Tools.** HLSL was compiled by glslang 15.1 to SPIR-V, and by Mesa 25.2.8's RADV/ACO to RDNA2
code, on a null Navi21 device (`RADV_FORCE_FAMILY=navi21`), with statistics from
`VK_KHR_pipeline_executable_properties`. Execution ran on lavapipe (llvmpipe, LLVM 20.1.2, 256-bit
SIMD) on a 4-core Xeon at 2.1 GHz. Neither is FXC or the Windows D3D12 driver, so read instruction
counts as indicative of the hardware, not of the owner's driver. lavapipe is a CPU JIT, like WARP,
so its timings stand in for WARP's, not a GPU's. Synthetic inputs: a 64³ lumpy-sphere SDF,
16,384 vertices on its surface, and `SDFMesh.cpp`'s direction pattern (46 × 46, radius 0.15).

**SDF AO, per sample in the inner loop, RDNA2.** lavapipe times are for 16,384 vertices × 2,116
samples, median of 5.

| Version | Instructions | VALU | Memory ops | Memory waits | lavapipe | Output vs today |
|---|---|---|---|---|---|---|
| Today | 175 | 75 | 9 | 5 | 288.6 ms | — |
| E2(a) | 117 | 73 | 9, issued together | 2 | 267.4 ms | bit-identical, 16,384 of 16,384 |
| E2(a)+(b) | 114 | 71 | 8 | 1 | 259.0 ms | bit-identical, 16,384 of 16,384 |
| D1 + E2(b) | 32 | 22 | 1 | 1 | 75.9 ms | ≤ 2.4e-7 (lavapipe filters in fp32) |
| D1 + E2(b)+(c) + fused transform | 29 | 19 | 1 | 1 | 76.2 ms | ≤ 2.4e-7 |

**Filter precision (D1).** The bake was emulated in float64 with sub-texel weights quantised to 8
bits, the minimum Direct3D allows. The AO moved by at most 8.2e-4, mean 2.5e-5, against 1/255 =
3.9e-3 per display step. With 6 bits: at most 3.6e-3. This was a synthetic shape, with AO from 0.79
to 1.0.

**`GenFieldCS`, per octave (one Worley evaluation of 27 cells), RDNA2.**

| Hash | VALU | `v_sin` | Quarter-rate integer multiplies | VGPRs |
|---|---|---|---|---|
| sin-based, today | 689 | 67 | 0 | 80 |
| Hoskins, float only | 664 | 0 | 0 | 96 |
| PCG3D | 859 | 0 | 153 | 96 |

**Plate AO, per vertex-triangle pair, RDNA2.** The loop was compiled as a compute shader.

| Version | Texture samples | Scalar or LDS loads | VALU |
|---|---|---|---|
| Today (filtered RGBA32F reads) | 2, plus 2 descriptor reloads | 0 | 27 |
| E3(a): constant buffer | 0 | 2 scalar | 24 |
| Groupshared tiles | 0 | 2 LDS | 26 |

**`GenFieldcopyCS`.** 181 → 39 instructions and 9 → 2 memory ops with D1's sampler.

**Not verified.**
- FXC's DXBC for any of this. Whether FXC flattens `Field.hlsli`'s bounds test, or hoists L6's
  uniform terms, needs `fxc /T cs_5_1 /Fc` (or `ps_5_1`), or RGA's DX12 mode, on Windows.
- The hull sizes behind E3's estimate.
- Every per-frame figure, which comes from counting operations and bytes.

## 7. Open for the owner

1. **Which GPU and configuration** the numbers are taken on. ADR-009 used an Iris Xe, in Debug with
   the debug layer.
2. **Whether WARP time is a goal.** E1 and E2(c) matter most there, and they shorten the smoke
   runs.
3. **Whether M1's timing lines stay committed,** as the field's does (ADR-009 decision 6), or stay
   a local edit for the measurements.
4. **D1 to D10,** one by one.
