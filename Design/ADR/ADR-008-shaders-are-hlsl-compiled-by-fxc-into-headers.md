# ADR-008: Shaders are HLSL compiled by FXC into headers, and reflected at load

- **Status:** Accepted (owner, 2026-09-25, at the start of the NeuronClient migration's Phase 3)
- **Scope:** the shaders of `lt.dll` (`FrontierOutpost/src/liblt/Shaders/`) and of NeuronClient
  (`NeuronClient/Shaders/`): their language, names and compilation, and how liblt finds them
- **Detail:** `Design/Plan/NeuronClient-migration.md` §2 (on N2), §3 (point 2), §4.2, §5.2, §5.4,
  §5.6, §10 and Phase 2 step 1, N2, N3 and N11

## Context

liblt's shaders are 169 `.jsl` files in `GameData/shader`, 6,357 lines of GLSL 1.20 with
`EXT_gpu_shader4`. The engine prepends `#version 120`, runs its own preprocessor for `#include`
and `#output`, and compiles them at run time (plan §4.2).

The owner decided that shaders are compiled at build time into headers (N2). The plan corrects two
points of what the question offered (plan §2):

- **FXC, not DXC.** liblt binds uniforms and textures by name, in 243 lines of 46 C++ files and 57
  lines of script, so it needs reflection at run time. DXIL, DXC's output, can only be reflected
  by `dxcompiler.dll`, a redistributable (AGENTS.md R14). DXBC, FXC's output, is reflected by
  `D3DReflect` in `d3dcompiler_47.dll`, which ships with Windows 10 and 11. Shader Model 5.1 also
  keeps Windows 10. Outpost.Commander's SM 6.7 raised its floor to Windows 11 22H2.
- **The script filters survive.** Their 19 `Shader_Create` calls
  (`GameData/script/Texture/Filters.lts`) name literal files, so a registry keyed by the legacy
  name covers them, and the observatory and image apps keep working.

N2 also costs two run-time features (plan §3, point 2): hot reload, and the shader code `SDFMesh`
generates per mesh, which ADR-009 replaces. FXC is frozen at SM 5.1; nothing in this content needs
more.

## Decision

1. **Shaders are HLSL,** flat in `FrontierOutpost/src/liblt/Shaders/` as `*.hlsl` and `*.hlsli`.
   NeuronClient's own (mip generation, present) live in `NeuronClient/Shaders/` and compile the
   same way (plan §5.6). **liblt's folder is outside ADR-001's exemption** (N11): AGENTS.md's
   shader rules and the checkers apply to it, and ADR-001 says so from the commit that adds its
   first file. The rest of `lt` stays exempt.
2. **A file is named from its legacy path and its stage** (AGENTS.md §2). Take the path below
   `GameData/shader/vertex/` or `fragment/`, drop `.jsl`, write it in PascalCase, and add `VS` or
   `PS`. Each `/` and `_` starts a word, and no other letter changes case. So `post/blur.jsl`
   becomes `PostBlurPS.hlsl`, and `compute/lensflare_visibility.jsl` becomes
   `ComputeLensflareVisibilityPS.hlsl`. A compute shader ends in `CS`. An included file is
   `.hlsli`, with no stage.
3. **FXC compiles each stage at build time:** one `FxCompile` item per stage in the owning
   `.vcxproj`, at Shader Model 5.1, the same in Debug and Release, into a header holding a byte
   array. The headers go to `CompiledShaders/`, which is build output and git-ignored. Nothing is
   compiled at run time (plan §2, §5.6).
4. **Reflection runs at load, through `D3DReflect`** (plan §5.2, §5.4). A program holds its
   bytecode, its constants, textures and samplers by name, and its input signature. liblt resolves
   names through it and caches them by their text. HLSL reserves `texture` and `sample`, and has
   `saturate` and `noise` as intrinsics. The HLSL renames them, and the reflection layer maps the
   GLSL names, so C++ and scripts keep writing `"texture"` (plan §5.6).
5. **A registry keyed by the legacy names** maps `identity.jsl`, `post/blur.jsl` and the rest to
   compiled programs. `Shader_Create(vs, fs)` looks both up; an unknown name is fatal and names
   the path. The project checker verifies that every `Shaders/*.hlsl` is registered, and that
   every registered name has a file (plan §5.6).
6. **One include replaces three.** `global.jsl`, `vert.jsl` and `frag.jsl` become `Common.hlsli`,
   which holds the clip-space macro of ADR-007, the output struct that replaces `#output`, and the
   HIGHQ/LOWQ switch.
7. **SMAA gets its own HLSL porting block back.** `smaa.jsl` is SMAA.h itself, and only its short
   porting block is GLSL (`smaa.jsl:373-385`). No third-party code is added, and SMAA's licence
   notice goes beside the file, which closes `FrontierOutpost/MIGRATION_NOTES.md` O14 (plan §5.6).
8. **What goes:** `JSLPreprocess`, the `#version` injection, hot reload (`Shader_RecompileAll`,
   and the T key that calls it in 7 apps), and `GameData/shader` (plan §2, §5.6).
9. **`d3dcompiler_47.dll` is proved where it runs.** A NeuronClient test loads it and reflects a
   compiled blob, and the owner runs it on the ARM64 device as well. If a target lacks the DLL,
   the fallback is reflection tables generated at build time (plan Phase 2 step 1, §10).

## What this forecloses

- **Compiling or reloading shaders at run time.** A shader change needs a build.
- **DXC and Shader Model 6 while reflection runs at load.** A later move to DXC must move
  reflection to build time as well, as decision 9's fallback does, since reflecting DXIL at run
  time means shipping `dxcompiler.dll`, a new dependency (AGENTS.md R14). A higher Shader Model can
  also raise the Windows floor, as SM 6.7 did for Outpost.Commander (plan §2, §3 point 2, §10).
- **A program the registry does not name.** C++ and scripts reach shaders only by their legacy
  names.
- **Shader source in `GameData/`.** ADR-004 is amended to say so (plan §8).
