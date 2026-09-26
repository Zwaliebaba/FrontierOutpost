# ADR-009: SDF fields are interpreted by a compute shader

- **Status:** Proposed (2026-09-25, NeuronClient migration Phase 0), for the owner to accept.
  Phase 4 step 4 implements it: it changed decisions 1 and 3 to what it found, and added to 4 and
  6 (2026-09-26).
- **Scope:** how `lt.dll` builds SDF fields: `LTE/SDFMesh.cpp` and the `LTE/SDF*.cpp` node types in
  `FrontierOutpost/src/liblt/`, and the shaders that fill the field and derive from it
- **Detail:** `Design/Plan/NeuronClient-migration.md` §2 (on N2), §3 (point 2), §4.2, §5.4, §5.7
  and §10, N2 and N3

## Context

`SDFMesh` compiles one GL program per mesh. Its field function is GLSL, generated from a randomly
seeded SDF tree and passed in as `#define FIELDFN <GLSL>` (`LTE/SDFMesh.cpp:199-200`). The field
fills a 3D texture by copying 2D slices through the CPU, and those slices are read back
synchronously (plan §4.2).

N2 compiles every shader at build time (ADR-008), and a program per random tree cannot be
enumerated then. So N2 does not carry generated code over but replaces it, and SDF fields need a
redesign (plan §2, §3 point 2).

Evaluating the tree on the CPU is not the way out. Every SDF node has a CPU `Evaluate`, but the
two noise nodes are `NOT_IMPLEMENTED` there (`LTE/SDF.cpp`), and fields reach about 256³ voxels
(`LTE/SDFMesh.cpp:27`). It would be slow, and it would first need the noise ported to C++ (plan
§5.7).

## Decision

1. **The tree becomes an instruction stream:** postfix, opcode and parameters, in the compute
   shader's constant buffer. An instruction encoder in liblt, `SDFT::Encode`, writes it where
   `GetCode` and `FIELDFN` did (plan §5.4, §5.7). Every thread reads the same instruction at the
   same time, which is what a constant buffer serves best, and NeuronClient binds no structured
   buffer; the SDFs that are meshed are one to three nodes, far inside the 64 instructions the
   shader holds. The Proposed text said a structured buffer.
2. **One precompiled compute shader walks the stream for each voxel,** with a value stack and a
   point stack, and writes the R32F 3D texture directly through a UAV. The slice-by-slice copy
   through the CPU goes.
3. **Occlusion, and the resampling of the field into each level of detail's grid, become compute
   passes.** The gradient pass goes: `SDFMesh` made its program and never ran it. Compute is given
   no samplers, so the passes filter the field themselves, trilinearly and with GL's border of 1.
   The Proposed text said gradient and occlusion.
4. **The LOD grids are read back asynchronously** for the CPU polygoniser. liblt's scheduler waits
   for the GPU after each run of a job to time it, so a grid is ready at its job's next run.
5. **The opcodes are the SDF node types that Phase 1 leaves** (ADR-013). Of the two noise nodes,
   only `FractalWorley` is constructed (`Game/Renderable/Asteroid.cpp:18`), so Worley noise is
   ported to HLSL once. `FractalPerlin`, which nothing constructs, goes in Phase 1.
6. **The interpreter's cost is measured, not assumed.** It is slower than code specialised per
   mesh, but it runs at generation time. Phase 4 measures generation time on WARP and on a GPU
   (plan §5.7, §10): `SDFMesh` logs how long each field took. On WARP, in Debug with the debug
   layer, a field of 128³ voxels took 22 s (21989, 22126 and 22106 ms for `war`'s three asteroids
   in the smoke job on `86634e8`); the figure on a GPU is the owner's to take.

## What this forecloses

- **Shader code generated at run time,** for SDF fields or anything else (ADR-008).
- **Building fields on the CPU** in place of the GPU.
- **Shapes identical to the OpenGL build's.** Different shapes are acceptable under N3 (plan §10).
- **A node type without an opcode.** A new SDF node type needs its opcode, in the encoder and in
  the compute shader.
