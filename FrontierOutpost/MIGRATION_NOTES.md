# FrontierOutpost migration notes

The migration of `ltheory-old` (C++ written 2012–2015; CMake; Windows build resurrected by its
author in July 2022) to hand-authored MSBuild. Everything lands in `FrontierOutpost.slnx` at the
repository root and the projects under `FrontierOutpost/`. The targets are toolset v145, C++23
for first-party code, x64 and ARM64, and Debug and Release.

This is a build-system and language-standard migration only. Namespaces, identifiers, files and
user-facing strings keep their original names. `ltheory-old-main/` is read-only.

**Status:** Phase 1 (structure) is written and checked in the container (§15). Its first build on
the host is in progress. Phase 2 (x64 parity) waits on the owner's FMOD Ex x64 files (D8, O8).

§1–§9 record Phase 0. §10–§14 are the running registers the brief asks for: deviations, code
changes, BEHAVIOUR-RISK, the modernisation backlog and open issues. §15 onward logs each later
phase.

---

## 1. Decisions

| # | Decision | Taken by | When |
|---|---|---|---|
| D1 | The complete original is upstream `JoshParnell/ltheory-old` at `0535d46`, with its SFML submodule at `192eb968` and its Git LFS objects, cloned into the git-ignored `_baseline_build/`. The committed `ltheory-old-main/` stays untouched; §4 logs every difference between the two. | owner | Phase 0 |
| D2 | Every build runs on GitHub-hosted `windows-latest` through `.github/workflows/migration.yml`, which triggers on pushes to the migration branch and nothing else. The session doing the migration is a Linux container with no MSVC. | owner | Phase 0 |
| D3 | For `FrontierOutpost/`, this migration task overrides AGENTS.md §1 (naming), §2 (flat directories), §4 (formatting) and §3's platform and compiler rules. `Design/ADR/ADR-001-frontieroutpost-is-a-legacy-import.md` records the exemption and its scope. | owner | Phase 0 |
| D4 | One solution file, `FrontierOutpost.slnx`, at the repository root. No `.sln` is produced. Every "`msbuild FrontierOutpost.sln`" in the task brief means `msbuild FrontierOutpost.slnx`. | owner | Phase 0 |
| D5 | The brief's "use plan mode" for Phase 0 cannot coexist with 0d (a build) and 0f (writing this file). It was read as: Phase 0 writes only this file, ADR-001, the migration workflow and its two helper scripts, and one `.gitignore` entry. | stated to owner, not objected to | Phase 0 |
| D6 | First-party C++23 is **`LanguageStandard=stdcpplatest`** (`/std:c++latest`, `_MSVC_LANG=202400`: C++23 plus the C++26 draft), which is what the brief's rule gives. The recommendation was `stdcpp23` (§3.3); the owner chose the literal rule. It also matches AGENTS.md §3. | owner | Checkpoint 0 |
| D7 | The dependency plan in §8.1 is approved as recommended. Its specifics are D8–D11, and ADR-002 records it. | owner | Checkpoint 0 |
| D8 | **FMOD Ex x64:** the owner supplies FMOD Ex 4.44.x's x64 import libraries and DLLs, ideally core 4.44.14 and event 4.44.20 to match the x86 pair. They go in `FrontierOutpost/extlib/x64/FMOD/` and `FrontierOutpost/extbin/x64/`, mirroring `extlib/win32` and `extbin/win32`. Until then, x64's link stops at the 28 FMOD symbols. | owner | Checkpoint 0 |
| D9 | **FMOD Ex on ARM64 is a documented blocker, acknowledged in advance.** ARM64 builds every project, but `lt` and `launch` cannot link. This satisfies the brief's Phase 4 gate ("or a documented blocker I have acknowledged"). | owner | Checkpoint 0 |
| D10 | **FreeType 2.5.5 and GLEW 1.7.0 are added as source, each with its licence (R14).** Our own `.vcxproj` builds each as a static library for both platforms. FreeType is logged as BEHAVIOUR-RISK against the Win32 original's 2.3.5. | owner | Checkpoint 0 |
| D11 | **Copy scope is the Windows-relevant subset (§8.3 item 4):** no other-OS binaries, no SFML examples, docs, tools or CMake files. | owner | Checkpoint 0 |
| D12 | **Runtime assets are committed as the LFS pointer files** they are in `ltheory-old-main/`. Nothing fetches the real objects, because D13 removes the smoke test. | owner | Checkpoint 0 |
| D13 | **The Phase 5 smoke test is dropped**, as the brief allows. Done means all four configuration/platform combinations compile and link, with D9's blocker as the one acknowledged exception. | owner | Checkpoint 0 |

## 2. Environment

The session is a Linux container with no `cl.exe`, `msbuild`, `vswhere` or Wine. It can edit files
and run Python, and it drives the build host through the GitHub API.

The build host is the GitHub-hosted runner, recorded by the workflow's `toolchain` job (run 2,
2026-09-24):

| Item | Value |
|---|---|
| Runner image | `win25-vs2026` 20260922.246.2, AMD64, 4 logical cores |
| Visual Studio | Enterprise 2026 **18.10.1** (18.10.12210.168), the only instance |
| MSVC toolsets on disk | 14.29.30133 (v142), 14.44.35207 (v143), **14.51.36231 (v145, default)** |
| `cl.exe` | 19.51.36256 / 36257 (x64, x86 and ARM64 targets) |
| v145 registered by MSBuild (`VC\v180`) for | Win32, x64, **ARM64**, **ARM64EC** |
| Windows SDK | **10.0.26100.0**, the only one installed; has `um` and `ucrt` libraries for x64, x86 and ARM64 |
| CMake | 4.4.3 on `PATH` (used for the baseline) and 4.3.1-msvc1 bundled with VS; both offer `Visual Studio 18 2026` |
| Git | 2.55.0.windows.5, git-lfs 3.7.1 |

### 2.1 Phase 0e: the v145 ARM64 build tools are installed

- `VC\Tools\MSVC\14.51.36231\bin\Hostx64\arm64\cl.exe` exists.
- `lib\arm64` and `lib\arm64ec` exist.
- `Microsoft.VisualStudio.Component.VC.Tools.ARM64` and `...ARM64EC` are installed.
- `cl` reports "for ARM64" when set up with `VsDevCmd -arch=arm64`.

The older v143 toolset (14.44) has **no** ARM64 compiler on this image. That doesn't matter, since
every project pins v145.

### 2.2 The host cannot run what it builds for ARM64

`windows-latest` is x64. ARM64 binaries can be built and linked there but not executed (see §8,
open issue O4).

## 3. Language standard

### 3.1 What the original compiled as

The original sets no standard on Windows. CMake's generated projects carry no `LanguageStandard`,
so MSVC's default applies: **C++14** (`_MSVC_LANG=201402`). Off Windows it passed `-std=c++0x`.
SFML 2.5.0 sets none either.

### 3.2 What `cl /?` documents (19.51.36257)

```
/std:<c++14|c++17|c++20|c++latest> C++ standard version
```

It documents no explicit C++23 switch.

### 3.3 What the compiler actually does

`probe.cpp` prints `_MSVC_LANG` and `__cplusplus`, built with `/Zc:__cplusplus`:

| Switch | Result |
|---|---|
| `/std:c++20` | `202002` |
| `/std:c++23` | **D9002 "ignoring unknown option"**, then compiles as C++14 (`201402`) |
| `/std:c++23preview` | accepted without a diagnostic: **`202302`, exactly C++23** |
| `/std:c++latest` | **`202400`**: C++23 plus the C++26 working draft |

The brief's rule ("the explicit C++23 switch `cl /?` exposes, else `/std:c++latest`") taken
literally selects `/std:c++latest`, which is **not C++23**. It is C++23 plus whatever C++26
features the toolset has implemented, and that set changes with toolset updates. The switch that
means C++23 exists and works, but `cl /?` doesn't list it. This is **D6**, for Checkpoint 0.

**MSBuild does expose it.** The v145 property rules
(`MSBuild\Microsoft\VC\v180\1033\cl.xml`, read in run 3) offer these `LanguageStandard`
values:

| `LanguageStandard` | Switch |
|---|---|
| `Default` | (compiler default, C++14) |
| `stdcpp14` | `/std:c++14` |
| `stdcpp17` | `/std:c++17` |
| `stdcpp20` | `/std:c++20` |
| **`stdcpp23`** | **`/std:c++23preview`** |
| `stdcpplatest` | `/std:c++latest` |

So the toolset has an explicit, first-class C++23 setting. It is exactly the switch `cl /?`
leaves out. The recommendation was `LanguageStandard=stdcpp23` (`/std:c++23preview`), because it
is C++23 and will not take on C++26 draft behaviour when the toolset is updated.

**Owner's decision (D6): `stdcpplatest`**, the literal rule. Phase 3 therefore compiles
first-party code as C++23 plus the C++26 draft (`_MSVC_LANG=202400` on 19.51). A later toolset
may move that set.

## 4. Provenance: the committed copy against upstream

`ltheory-old-main/` (committed in `6d8b20e` "Sync") is **blob-identical** to upstream
`JoshParnell/ltheory-old` at `0535d46d04db78ecb2bcf532f79a8b366757bb3d` (2022-07-24) in all 1,609
files it contains. Upstream has **ten entries the committed copy lacks**:

| Entry | Why it is missing |
|---|---|
| `ext/SFML` (gitlink to SFML `192eb968a4e938f36948e97f97ddc354a8a470fe`, SFML 2.5.0, 2018-05-06) | A submodule. The copy has neither the files nor the gitlink. |
| `extbin/win32/fmod_event.dll`, `fmodex.dll`, `freetype6.dll`, `zlib1.dll` | The repository's own `.gitignore:30` (`[Ww][Ii][Nn]32/`) silently excluded them at commit time. |
| `extlib/win32/FMOD/fmod_event.lib`, `FMOD/fmodex_vc.lib`, `Glew/glew32s.lib`, `freetype/freetype.lib`, `freetype/freetype28s.lib` | Same rule. |

The copy has more defects than the missing entries:

- **284 files are Git LFS pointer stubs**, not content: 90 `.ttf`, 79 `.ogg`, 51 `.wav`, 26 `.so`,
  26 `.dylib`, 4 `.jpg`, 3 `.png`, 2 `.fsb`, 2 `.bin`, 1 `.otf`. They are identical to upstream's
  stored pointers. Upstream keeps these files in LFS, and whatever the copy was made from never
  fetched them. The build needs none of them. The runtime needs the fonts, sounds, textures and
  `.bin` data.
- **`src/resource.h` is corrupted on every checkout of this repository.** It is UTF-16LE, and the
  root `.gitattributes:78` (`*.h text eol=crlf`) makes Git insert a CR before every LF byte: the
  stored blob is 906 bytes, the checked-out file 923 bytes. The blob itself is intact, so a copy
  must come from the blob (or from upstream), not from a checked-out working tree. The same rule
  will hit `FrontierOutpost/src/resource.h` unless the copy is given its own attribute. `resources.rc`
  is unaffected (`*.rc -text`). No other UTF-16 file matches a forced-text rule.
- `src/liblt/LTE/LTE.h:93` includes `"Text.h"`, which exists **nowhere**, upstream included. This
  is harmless: `LTE.h` is included only by the seven programs under `src/old/`, and no build target
  compiles those.

## 5. Phase 0a: the CMake build

### 5.1 Build files

| File | Role |
|---|---|
| `CMakeLists.txt` (205 lines) | The whole first-party build. |
| `cmake/FindGLEW.cmake` | **Dead.** `CMAKE_MODULE_PATH` is never set and `find_package(GLEW)` is never called, so `${GLEW_GLEW_LIBRARY}` in the link list expands to nothing. GLEW reaches the link only through the `extlib/win32` glob. |
| `configure.py` | The author's build driver: configures with `cmake -A Win32 -S ./ -B ./build` (**32-bit only**), builds with `--config RelWithDebInfo`, runs `bin/launch.exe <app>` from the repository root, and `clean` removes `bin/`, `build/` and `cache/`. |
| `ext/SFML/CMakeLists.txt` and its includes | SFML 2.5.0: `cmake/Config.cmake`, `cmake/Macros.cmake`, `src/SFML/CMakeLists.txt`, and one each for System, Main, Window, Network, Graphics and Audio. `cmake/Modules/Find{Freetype,Vorbis,FLAC}.cmake` are used on Windows; `Find{EGL,GLES,UDev}.cmake`, `examples/`, `doc/` and `cmake/toolchains/` are not reached (`SFML_BUILD_EXAMPLES` and `SFML_BUILD_DOC` are FALSE). |

Nothing uses `configure_file` on Windows. SFML's pkg-config templates are processed only where
pkg-config exists, and its `install()` and `export` rules play no part in a build.

Two committed sources were produced by **manual generators that are not part of the build**:

- `src/liblt/LTE/DeclareFunction.h`, from `script/meta/DeclareFunction.py`, which writes to a
  hard-coded `/home/josh/lt/...`.
- `src/liblt/Module/MusicEngine/LtheoryTest01.h`, from FMOD Designer 4.44.20.

`script/` also holds `tloc.py` (a line counter), `assetlist.py` and `install_dependencies.sh`.
Nothing in the build calls any of them.

### 5.2 Targets on Windows with MSVC

| Target | Type | Output | Sources |
|---|---|---|---|
| `lt` | SHARED | `bin/lt.dll`, with import library `bin/lt.lib` (exports through `LT_API` = `__declspec(dllexport)`, `src/liblt/Common.h:45`) | `GLOB_RECURSE src/liblt/*.cpp`: 382 files (§5.6) |
| `launch` | EXECUTABLE (console) | `bin/launch.exe` | `src/launch/launch.cpp`, `src/resources.rc` |
| `sfml-system` | STATIC | `sfml-system-s-d.lib` / `sfml-system-s.lib` | SFML `System` plus `System/Win32` |
| `sfml-main` | STATIC | `sfml-main-d.lib` / `sfml-main.lib` | `Main/MainWin32.cpp` |
| `sfml-window` | STATIC | `sfml-window-s-d.lib` / `-s.lib` | SFML `Window` plus `Window/Win32` |
| `sfml-network` | STATIC | `sfml-network-s-d.lib` / `-s.lib` | SFML `Network` plus `Network/Win32` |
| `sfml-graphics` | STATIC | `sfml-graphics-s-d.lib` / `-s.lib` | SFML `Graphics` |
| `sfml-audio` | STATIC | `sfml-audio-s-d.lib` / `-s.lib` | SFML `Audio` |

The SFML libraries land in `build/ext/SFML/lib/<Config>/`. `sfml-main` and `sfml-audio` are built
by `ALL_BUILD` but **linked by nothing**. The per-target lists of SFML sources are taken from the
generated projects (§9).

**`bin/` is shared by every configuration.** `configure_output_dir()` points the RUNTIME,
LIBRARY and ARCHIVE output directories, and each per-configuration variant, at
`${CMAKE_SOURCE_DIR}/bin`. So Release overwrites Debug, and the build writes **into the source
tree**. That is why the baseline builds a clone and never `ltheory-old-main/`.

### 5.3 Link graph

- `lt` links `${LINK_LIBRARIES}` then `${EXTLIB}`:
  - `LINK_LIBRARIES` is `sfml-graphics sfml-network sfml-system sfml-window` plus
    `${OPENGL_LIBRARIES}` (`opengl32`, `glu32`). `${GLEW_GLEW_LIBRARY}` is empty.
  - `EXTLIB` is `GLOB_RECURSE extlib/win32/*.lib` = `FMOD/fmod_event.lib`, `FMOD/fmodex_vc.lib`,
    `Glew/glew32s.lib`, `freetype/freetype.lib`, `freetype/freetype28s.lib`.
- SFML's link dependencies propagate through its static libraries:
  - `sfml-system` pulls in `winmm`.
  - `sfml-window` pulls in `opengl32`, `winmm` and `gdi32`.
  - `sfml-network` pulls in `ws2_32`.
  - `sfml-graphics` pulls in `opengl32` and **SFML's own bundled static FreeType 2.5.5**
    (`ext/SFML/extlibs/libs-msvc-universal/<x86|x64>/freetype.lib`).
- `launch` links the same `${LINK_LIBRARIES}` plus `lt`.

**The link of `lt.dll` therefore carries three FreeType libraries**:
- SFML's static 2.5.5;
- `freetype.lib`, the import library of `freetype6.dll`;
- `freetype28s.lib`, a static 2.8 by its name.

`liblt` compiles against a fourth version's headers (2.5.3, `include/FreeType`). Link order
decides, and §9.4 shows the outcome: on Win32 every FreeType call binds to `freetype6.dll`,
**FreeType 2.3.5**.

### 5.4 Compile and link settings

**`lt` and `launch`**, from the top-level directory:

- **Include directories:** `src/liblt`, `include`, `include/FreeType`, `ext/SFML/include`, plus
  SFML's public include through linking.
- **Definitions:** `SFML_STATIC` (a PUBLIC usage requirement of every SFML library), CMake's
  `WIN32` and `_WINDOWS`, `NDEBUG` in Release, and `lt_EXPORTS` for `lt` (CMake's DEFINE_SYMBOL,
  unused by the code).
- **Flags:** CMake's defaults (`/W3 /GR /EHsc`; Debug `/MDd /Zi /Ob0 /Od /RTC1`; Release
  `/MD /O2 /Ob2 /DNDEBUG`), and then the CMakeLists appends **`/EHs /MP /fp:fast /arch:SSE2`**.
  - The comment above `/EHs` says "No exception handling". It enables exception handling.
  - `/arch:SSE2` is valid on x64 too (`cl /?` lists it, the baseline raises no D9002) and equals
    the x64 default. It is not valid for ARM64.
- **Runtime library:** `/MD` and `/MDd`. SFML matches, because `SFML_USE_STATIC_STD_LIBS` is
  FALSE.
- **`lt` link flag:** `/NODEFAULTLIB:libcmt`. The loop meant to give `launch` the same flag
  iterates `${EXECUTABLES}`, which is undefined, so `launch` gets nothing.
- **Linker defaults:** `/machine:<arch>`; Debug `/debug /INCREMENTAL`; Release `/INCREMENTAL:NO`.
- **Subsystem:** console. `launch.cpp` has `main`, and its `/SUBSYSTEM:windows` pragma sits under
  `#if 0`.
- **Precise per-configuration values** are taken from the generated projects (§9).

**SFML**, set directory-wide by its own CMake:

- `_CRT_SECURE_NO_DEPRECATE` and `_SCL_SECURE_NO_WARNINGS` (MSVC).
- `UNICODE` and `_UNICODE` for the Window module.
- `STBI_FAILURE_USERMSG` for Graphics.
- `FLAC__NO_DLL` and `OV_EXCLUDE_STATIC_CALLBACKS` for Audio.
- `SFML_STATIC`, since the parent forces `BUILD_SHARED_LIBS` FALSE.
- Private include of `src/`, and of `extlibs/headers/{stb_image,freetype2,AL,ogg,vorbis,FLAC}` as
  each module needs.
- `SFML_GENERATE_PDB` is TRUE, giving compile PDBs `sfml-<module>-s.pdb`.
- SFML's `Config.cmake` has no case for MSVC after 19.00. `SFML_MSVC_VERSION` stays unset, the
  `LESS 14` test is false, and the bundled libraries come from `extlibs/libs-msvc-universal/`,
  exactly as for the author's 2022 build.

### 5.5 Custom commands and post-build

- `lt` POST_BUILD, MSVC only: `cmake -E copy_if_different` of each `extbin/win32/*.dll`
  (`fmod_event.dll`, `fmodex.dll`, `freetype6.dll`, `zlib1.dll`) into `bin/`.
- No other custom command, and no code generation in the build.

### 5.6 The expanded glob

`file(GLOB_RECURSE LIBLT_SRC src/liblt/*.cpp)` expands to **382 files**. CMake sorts glob
results; they are listed per directory in that order:

- `src/liblt/AI/` (1): NLPPhrase
- `src/liblt/Audio/` (4): Envelopes, Filters, IO, Signal
- `src/liblt/Audio/Signal/` (4): Compress, Delay, Instrument, Lowpass
- `src/liblt/` (1): Common
- `src/liblt/Component/` (34): Account, Asset, Assets, Attachable, BoundingBox, Cargo, Collidable, Damager, Dockable, Drawable, Economy, Explodable, History, Info, Integrity, Interior, Log, Market, Motion, MotionControl, Nameable, Orientation, Pilotable, Pluggable, Projects, ProximityTracker, Queryable, Resources, Scriptable, Sockets, Storage, Targets, Tasks, Zoned
- `src/liblt/Game/Action/` (1): Mine
- `src/liblt/Game/` (18): Beam, Camera, Icons, Item, ItemProperty, Light, Materials, Messages, Mission, NLP, Object, Order, Particles, Player, Project, ShadingModels, Task, Universe
- `src/liblt/Game/Condition/` (1): Nearby
- `src/liblt/Game/Event/` (3): Damage, Deposit, Mined
- `src/liblt/Game/Graphics/` (3): Effects, Generators, RenderStyles
- `src/liblt/Game/Graphics/Generator/` (6): Blur, IRMap, Nebula, NormalMap, PlanetSkybox, PlanetSurface
- `src/liblt/Game/Item/` (20): AssemblyChip, Blueprint, ColonyType, Commodity, DataDamaged, DataDestroyed, OreType, PlanetType, PowerGeneratorType, ProductionLabType, ScannerType, ShieldType, ShipType, StationType, TechLabType, ThrusterType, TransferUnitType, TurretType, WeaponType, Worker
- `src/liblt/Game/ItemProperty/` (1): Owner
- `src/liblt/Game/Object/` (34): Asteroid, Colony, Custom, DroneBay, DustFlecks, Dynamic, Explosion, Missile, Payload, Planet, Pod, PowerGenerator, ProductionLab, Pulse, Rail, Region, Scanner, Shield, Ship, SoundEmitter, Star, Static, Station, System, TechLab, Thruster, Trail, TransferUnit, Turret, WarpNode, WarpRail, Weapon, Wormhole, Zone
- `src/liblt/Game/Object/Drone/` (2): Construction, Prospecting
- `src/liblt/Game/RenderPass/` (13): Blended, Camera, Clear, DepthPrepass, DustClouds, GBuffer, GlobalLighting, LensFlares, LocalLighting, Particles, SMAA, SSAO, Visibility
- `src/liblt/Game/Renderable/` (4): Asteroid, Ice, Imposter, Starfield
- `src/liblt/Game/ScriptAPI/` (6): Camera, Item, Object, ObjectComponents, Player, Task
- `src/liblt/Game/Task/` (19): Buy, Custom, Destroy, Dock, Drill, Goto, LOD, Manage, Mine, Mint, Patrol, Pirate, Play, Produce, Research, Sell, Spawn, Transport, Wait
- `src/liblt/Game/Widget/` (1): HUD
- `src/liblt/LTE/` (72): Archive, Axis, Buttons, CollisionMesh, Color, Config, CubeMap, Debug, Diff, DrawState, Expression, Font, Function, Geom, Grammar, Joystick, Keyboard, LTSL, Loader, Location, Lock, Math, Mesh, Meshes, Model, Module, Mouse, OS, Package, ParticleSystem, Patch, PlateMesh, Profiler, Program, ProgramLog, RNG, RenderPass, RenderPasses, RenderStyle, Renderable, Renderer, ResourceMap, SDF, SDFMesh, Script, ScriptFunction, Serializer, Shader, ShaderInstance, SpatialPartition, SpatialPartition_HashGrid, SpatialPartition_UniformGrid, SpatialSignature_UniformGrid, SphereTree, StackFrame, Static, String, StringList, StringTree, Texture2D, Texture3D, Thread, Time, Timer, Transform, Type, UniString, V2, V3, Viewport, Warp, Window
- `src/liblt/LTE/Button/` (5): And, Joystick, Key, Mouse, Or
- `src/liblt/LTE/Expression/` (25): Access, Address, Array, Assign, Block, Cast, Constant, Constructor, Conversion, Declare, Dereference, DynamicDispatch, ExpressionCall, For, Function, FunctionCall, If, List, Noop, Print, Reference, Switch, Type, Variable, While
- `src/liblt/LTE/RenderPass/` (6): Bloom, BloomLight, Composite, CustomFilter, MotionBlur, RadialBlur
- `src/liblt/LTE/SDF/` (21): Add, Box, Capsule, Cylinder, Expand, Intersection, Mirror, Multiply, Pinch, Polyhedron, Radial, Repeat, Ring, Scale, Shell, Sphere, Subtract, Torus, Translate, Union, Wedge
- `src/liblt/LTE/ScriptAPI/` (27): Bool, Data, Double, Expression, Float, Grammar, Int, Keyboard, List, Mesh, Model, OS, PlateMesh, RNG, Ray, Renderable, Renderer, ShaderInstance, String, StringList, StringTree, Texture2D, Thread, Timer, V2, V3, V4
- `src/liblt/LTE/Type/` (2): Array, Pointer
- `src/liblt/LTE/Warp/` (2): Attractor, Custom
- `src/liblt/Module/` (7): FrameTimer, MusicEngine, PhysicsEngine, Scheduler, Settings, SettingsEntry, SoundEngine
- `src/liblt/Module/ScriptAPI/` (1): SoundEngine
- `src/liblt/Module/SoundEngine/` (2): Fmod, Null
- `src/liblt/Strukt/CodeObject/` (1): Custom
- `src/liblt/ThirdParty/` (2): KDTree, TriTriOverlap
- `src/liblt/UI/` (7): ClipRegion, Cursor, Glyph, Icon, Interface, Widget, WidgetRenderer
- `src/liblt/UI/Compositor/` (3): Basic, Custom, None
- `src/liblt/UI/Glyph/` (9): Arc, Box, Circle, Gradient, Grid, Line, Rect, Ring, Triangle
- `src/liblt/UI/ScriptAPI/` (4): Glyph, Icon, Interface, Widget
- `src/liblt/UI/Widget/` (6): Custom, Dynamic, Layer, List, Rendered, Stack
- `src/liblt/UI/WidgetRenderer/` (1): DrawRenderable
- `src/liblt/Volume/` (3): Array3D, Contour, MarchingCubes

### 5.7 Platform-conditional logic

- **OS detection:** `WINDOWS`, `LINUX` (also FreeBSD) or `MAC`, otherwise fatal.
- **Architecture:** `check_type_size(void*)` sets `ARCH_32` or `ARCH_64`, which only chooses
  `extbin/linux32` or `linux64` on Linux. **On Windows the architecture changes nothing.** The
  libraries always come from `extlib/win32`, which is x86 (§6).
- **Windows:** the `extlib` glob, the link list, `/EHs /MP /fp:fast /arch:SSE2`, and
  `/NODEFAULTLIB:libcmt` on `lt`.
- **MSVC:** the DLL post-build copy.
- **Elsewhere:** `-std=c++0x -fno-exceptions -O2 -g -pedantic -Wall -Wextra -Werror` (with seven
  `-Wno-*`), `-Wfatal-errors`, `-msse -msse2`, and the gold linker on Linux.
- **In the code:** `src/liblt/Common.h:25-41` detects the OS. Lines 87-97 detect the
  architecture, but test `_WIN32` first, which is defined on every Windows target, so x64 and
  ARM64 builds also define `ARCH_32` (§7).

### 5.8 Runtime contract (for the smoke test)

- `launch.exe` requires **exactly one argument**, the LTSL app name, resolved as
  `resource/script/App/<name>`. Without one it prints an error and exits 0. The README's example
  is `war`.
- **Working directory:** the repository root (the parent of `resource/`). `configure.py run` starts
  it there as `bin/launch.exe`. If `resource/` isn't in the working directory it changes to `../`
  once (`launch.cpp:29`), so starting it from `bin/` works too.
- At start it creates a 1920×1080 window with an OpenGL context through SFML, initializes GLEW, and
  loads FMOD Ex (`SoundEngine_Fmod()`) and the physics module.
- **Everything it writes goes under `./cache/`**, relative to the working directory
  (`OS_GetUserDataPath()`, `OS.cpp:123-129`):
  - `config.txt` and `settings.bin`;
  - the logs `logErrors.txt` and `logAsserts.txt`;
  - `crashdumps/` (MiniDumpWriteDump, with DbgHelp loaded at runtime);
  - `screenshot/N.png` (F1);
  - script caches `<name>_<hash>.bin`.
- It reads the `resource/` tree, including `resource/texture/SMAA_*.bin`, which are LFS objects.
- Runtime DLLs, copied next to the executable: `fmodex.dll` and `fmod_event.dll` (both imported
  by `lt.dll`), `freetype6.dll` (imported, FreeType 2.3.5) and `zlib1.dll` (which `freetype6.dll`
  needs).

## 6. Phase 0b: dependencies

| Dependency | Location | Kind | Architectures | Version | Standard | x86-specific code | Licence file | Used by |
|---|---|---|---|---|---|---|---|---|
| **SFML** | `ext/SFML` (submodule) | vendored source, built by `add_subdirectory` | source; bundled extlibs **x86 and x64 only** | 2.5.0 (`192eb968`) | C++03-era, compiled at MSVC's default (C++14) | none in `src/`; `stb_image.h` v2.16 has an SSE2 path guarded by `_M_IX86`/`_M_X64` with a scalar fallback | `ext/SFML/license.md` (zlib/png; lists the extlibs' licences) | `lt`, `launch` (graphics, network, system, window) |
| SFML's bundled **FreeType** | `ext/SFML/extlibs/libs-msvc-universal/{x86,x64}/freetype.lib` and `headers/freetype2` | prebuilt static | x86, x64 | 2.5.5 | C | — | FTL/GPLv2 per SFML `license.md`; no text | `sfml-graphics`, and so `lt` |
| SFML's bundled **OpenAL-Soft, FLAC, Ogg, Vorbis** | same folder; `extlibs/bin/<arch>/openal32.dll` | prebuilt (OpenAL is an import library) | x86, x64 | — | C | — | LGPL (OpenAL), BSD, per `license.md` | `sfml-audio` only, **which nothing links** |
| **FMOD Ex** | `include/FMOD` (headers); `extlib/win32/FMOD/{fmodex_vc,fmod_event}.lib`; `extbin/win32/{fmodex,fmod_event}.dll` | prebuilt, **proprietary**, discontinued | **x86 only** (dumpbin); no ARM64 build of FMOD Ex has ever existed | headers and `fmodex.dll` 4.44.14 (`FMOD_VERSION 0x00044414`); `fmod_event.dll` 4.44.20 | C/C++ API | binaries only | **none** in the tree | `src/liblt/Module/SoundEngine/Fmod.cpp` |
| **FreeType** (liblt's own) | `include/FreeType` (2.5.3 headers); `extlib/win32/freetype/{freetype.lib, freetype28s.lib}`; `extbin/win32/freetype6.dll` | prebuilt: an import library and a static library | **x86 only** | headers 2.5.3; **the DLL, which is what runs, 2.3.5**; `freetype28s` suggests 2.8 and contributes nothing (§9.4) | C | `ftconfig.h` has inline asm for `_M_IX86` and GCC x86/ARM, with a C fallback | none | `src/liblt/LTE/Font.cpp` |
| **zlib** | `extbin/win32/zlib1.dll` | prebuilt DLL | **x86 only** | 1.2.11 | C | — | none | runtime dependency of `freetype6.dll` |
| **GLEW** | `include/Glew/GL/{glew,wglew,glxew}.h`; `extlib/win32/Glew/glew32s.lib` | prebuilt static (`GLEW_STATIC` is defined in `LTE/GL.h:28`) | **x86 only** | 1.7–1.8, judging by the header (up to GL 4.2) | C | `glew.h` calling-convention blocks for `_M_IX86`, with an `else` for other targets | none (the header comment names BSD/MIT/Khronos) | `LTE/GL.h`, `GLEnum.h`, `Renderer.cpp` |
| **OpenGL / GLU** | Windows SDK (`opengl32`, `glu32`) | system | all | — | — | — | — | `lt`, SFML |
| Windows SDK import libraries | CMake's default set (`kernel32 user32 gdi32 winspool shell32 ole32 oleaut32 uuid comdlg32 advapi32`) | system | all | 10.0.26100.0 | — | — | — | `lt` (`SHGetFolderPath`, `CreateProcess`, `MessageBoxA`, ...) |
| **DbgHelp** | `DbgHelp.dll` via `LoadLibrary` (`OS.cpp:202`) | system, loaded at runtime | all | — | — | — | — | crash dumps; no link dependency |
| **utfcpp** | `include/UTF8` | vendored header-only | portable | 2006 (Trifunovic) | C++98 | none | none (the header comment says BSL-1.0) | `LTE/UniString.cpp` |
| **dirent for Windows** | `include/windirent.h` | vendored header | see §7, H3 | 1.11 (Ronkko, 2011) | C | defines `_X86_` only for `_M_IX86` | none (MIT in the header) | `LTE/OS.cpp` |
| **Microsoft `GL.H` / `GLU.H`** | `include/GL/GL.H`, `GLU.H` | vendored 1996 Platform SDK headers | portable | 1985-96 | C | none | none | shadows the SDK's `<GL/gl.h>` and `<GL/glu.h>` for `lt` (case-insensitive `/I include`) |
| **kdtree** | `src/liblt/ThirdParty/KDTree.{h,cpp}` | vendored source compiled into `lt` | portable | 2007-2011 (Tsiombikas) | C/C++ | none | none (BSD in the header) | `lt` |
| **Tri-tri overlap** | `src/liblt/ThirdParty/TriTriOverlap.cpp` | vendored source compiled into `lt` | portable | Möller/OPCODE-derived | C++ | none | none | `lt` |
| SMAA lookup-texture sizes | `src/liblt/ThirdParty/SMAA_*Tex.h` | size macros only; the data is `resource/texture/SMAA_*.bin` (LFS) | — | — | — | — | none | `Game/RenderPass/SMAA.cpp` |
| **enet, OVR (Oculus), GLUT, GLUI, GLAux** | `include/enet`, `include/OVR`, `include/GL/{glut,glui,GLAux}.h`; enet `.so`/`.dylib` in `extbin` | vendored headers | — | — | — | `glut.h` x86 pragmas | none | **nothing that is built** |
| RAD Telemetry | `telemetry/telemetry.h`, **absent** | — | — | — | — | — | — | `Profiler.cpp`, only under `LIBLT_LINUX` and a commented-out `USE_TELEMETRY` |

The licence of ltheory-old itself is the Unlicense (`LICENSE`, upstream commit `940a207`). Of the
fonts, only `resource/font/DroidSans` ships a licence file.

## 7. Phase 0c: ARM64 hazards in first-party code

The scanned tree is the built first-party code: `src/liblt` (382 `.cpp`, 340 `.h`) and
`src/launch`. The unbuilt `src/old/` was scanned separately and had **no hits at all**.

The categories scanned: intrinsics headers (`*mmintrin.h`, `intrin.h`, `arm_neon.h`), SIMD types
and `_mm_*` calls, inline assembly, architecture macros, x86-only builtins (`__rdtsc`,
`__cpuid`, `__popcnt`, `_mm_pause`, ...), FP-control calls (`_controlfp`, `_MCW_PC`, MXCSR),
`volatile`, atomics and Interlocked calls, alignment and packing, calling conventions, and
pointer↔integer casts.

**The code has no intrinsics, SIMD, inline assembly, x86-only builtins, FP-control calls, atomics,
Interlocked calls, alignment pragmas or explicit calling conventions.** Every hit:

| # | Location | Hazard | Assessment |
|---|---|---|---|
| H1 | `src/liblt/Common.h:87-97` | `#if _WIN32 \|\| _WIN64` / `#if _WIN32` → `ARCH_32`. `_WIN32` is defined on every Windows target, so x64 and ARM64 also get `ARCH_32`. | The only consumer is `LTE/Hash.h:7`, which uses the 32-bit FNV constants unless `ARCH_64` **and** `ALLOW_64_HASH` are both defined (the latter is commented out). The misdetection is behaviour-neutral: every Windows target hashes identically. **No change planned.** |
| H2 | `src/liblt/Common.h:101` | `#if __x86_64__` (GCC branch) | Not compiled by MSVC. |
| H3 | `include/windirent.h:95` | Defines `_X86_` only for `_M_IX86`, then includes `<windef.h>`/`<winbase.h>` directly. For x64 and ARM64 `winnt.h` would raise "No Target Architecture". | It works only because `OS.cpp:16` includes `Shlobj.h` (and with it `<windows.h>`, which defines `_AMD64_`/`_ARM64_`) first. **Order-sensitive: must keep the include order.** |
| H4 | `src/liblt/Common.h:13` | `typedef unsigned long ulong;` | 32-bit on every Windows target (LLP64), so there is no x86/x64/ARM64 difference. A hazard only for a `ulong` that holds a pointer. The scan found none, and the x64 compile reports no C4311, C4302 or C4312 (§9.4). |
| H5 | `src/liblt/LTE/Hash.h:26-28` | `Hash(T const&)` is FNV over the raw bytes of `sizeof(T)`. | For a `T` holding pointers, 64-bit targets hash 8 bytes a pointer and Win32 hashed 4. Deterministic within one build. Only matters if hashes cross architectures; they key the script caches `cache/<name>_<hash>.bin` (`ScriptFunction.cpp:16`), which are rebuilt locally. |
| H6 | `LTE/Type.h:466`, `LTE/Type/Pointer.cpp:29`, `LTE/DataStack.h:6` | The script type system sizes pointer types and stack alignment as `sizeof(void*)`. | Correct on every target. Data laid out by LTSL is pointer-size dependent, so script caches written by Win32 builds are not interchangeable with 64-bit ones. |
| H7 | `LTE/Model.h:16` | `return (size_t)this;` | Lossless on 64-bit. |
| H8 | `LTE/Vector.h:315` | `return (int)this->size();` | `size_t`→`int` narrowing: new truncation on 64-bit, not ARM64-specific. |
| H9 | `LTE/Common.h:278`, `LTE/InternalList.h:62-63`, `LTE/Type.h:136-137` | `offsetof` by dereferencing null through `volatile char*` | Undefined behaviour on every target, but MSVC handles it on all three. The `volatile` has no synchronization role. |
| H10 | `LTE/Function.h:61,75`, `LTE/Type.h:107,119` | `volatile static` self-registration objects | They keep static initializers from being discarded; no synchronization role. MSVC's ARM64 default `/volatile:iso` does not affect them. |

There are also **generic floating-point differences**, which apply to every float-heavy translation
unit:

- The original compiles with `/fp:fast`. On ARM64 the compiler may contract `a*b+c` into `fmadd`.
  On x64 it cannot without `/arch:AVX2`.
- Out-of-range float→int conversions saturate on ARM64, while x86 returns `0x80000000` (see also
  MSVC `/fpcvt`).

Together these make ARM64 results **not bit-identical** to x64. That doesn't matter for building,
but it does matter for any gameplay determinism. It will be recorded as BEHAVIOUR-RISK when Phase 4
enables ARM64.

**Pointer-size assumptions the scan cannot see** come from the compiler instead. The x64 baseline
compiles every translation unit and reports **zero C4311, C4302 or C4312**, so no pointer is
truncated to a smaller integer or widened from one. What 64-bit does add is **360 C4267s**
(`size_t` narrowed to `int`/`uint`, H8's kind), 358 of them in `lt` (§9.4). Those are
length-truncation hazards only for containers or files beyond 4 GiB. They are recorded as a
warning category, not fixed (Phase 3 fixes only errors).

## 8. Checkpoint 0: recommended handling per dependency

**The central fact:** the original's Windows dependencies are prebuilt, x86-only binaries, and the
original has never been built for anything but x86.
- **x64** needs a new source for **FMOD Ex and GLEW**: they are the whole of its link failure
  (§9.1). FreeType already resolves on x64, to SFML's bundled 2.5.5.
- **ARM64** needs a source for those two **and FreeType**. For FMOD Ex none exists (§8.2).

### 8.1 Plan per dependency

The options per dependency are: our own `.vcxproj` built from vendored source, prebuilt binaries,
or vcpkg manifest mode.

| Dependency | Recommended | Why | x64 | ARM64 |
|---|---|---|---|---|
| **SFML 2.5.0**, six modules | **Own `.vcxproj` per module, from the vendored source** (the `ext/SFML` tree at `192eb968`) | It is what the original does (it builds the submodule), so version, sources and flags carry over exactly. The Win32 platform code is portable C++. vcpkg only offers 2.6/3.x. | yes | **yes** for the libraries themselves. `sfml-graphics` needs FreeType at `lt`'s link (below). `sfml-audio` is linked by nothing, so building it needs only headers, and OpenAL, FLAC, Vorbis and Ogg are never linked. |
| **FreeType** | **Own `.vcxproj` building FreeType 2.5.5 from vendored source**: one static library, linked by `lt`. This adds FreeType's source and its FTL licence text: **R14 approval**. **BEHAVIOUR-RISK** (§12): the Win32 original draws its text with 2.3.5 (§9.4). FreeType 2.4 made the TrueType bytecode interpreter the default hinter, so glyphs may render visibly differently. | 2.5.5 is exactly what the original's own CMake build links **on x64**, the brief's baseline architecture (§9.4). It is SFML's bundled version, so `sfml-graphics`' headers match, and it is ABI-compatible with the 2.5.3 headers `liblt` compiles against. One source gives x64 and ARM64. | yes | **yes** |
| FreeType, alternative 0 | Build 2.3.5 from source instead: the version the Win32 original runs | Rendering parity with Win32, but a 2007 release with known font-parsing CVEs, and still mismatched with `liblt`'s 2.5.3 headers, as the original was | yes | yes |
| FreeType, alternative A | Prebuilt: SFML's bundled static 2.5.5 for x64, which is exactly what the original's x64 link resolved to | No new source, and Phase 2 needs nothing more. But ARM64 would then need a source build anyway, of the same version. | yes | **no** |
| FreeType, alternative B | vcpkg `freetype` (2.13.x) | Has ARM64, but `liblt` includes its own 2.5.3 headers by path, so the headers and the library disagree unless the include paths change. Also adds a package manager (R14). | yes | yes |
| **GLEW** | **Own `.vcxproj` from GLEW source matching the vendored headers** (1.7.0 or 1.8.0, to be pinned by diffing `glew.h`). It is one `glew.c`, built static with `GLEW_STATIC` as `glew32s.lib` was. Adds `glew.c` and its licence text: **R14 approval** | Header and library from the same release. Portable C. | yes | **yes** |
| GLEW, alternatives | Prebuilt: official 1.x binaries are Win32/x64 only. vcpkg: 2.2.0, a version skew against the vendored 1.x header. | — | yes | prebuilt **no**, vcpkg yes |
| **FMOD Ex 4.44.14** | **Prebuilt binaries: the only option**, since it is closed source and has no package. x64 needs FMOD Ex 4.44.x's own x64 files (`fmodex64_vc.lib`, `fmod_event64.lib`, `fmodex64.dll`, `fmod_event64.dll`), **which are neither in the tree nor upstream: the owner must supply them.** | — | **only with owner-supplied binaries** | **NO PATH** (§8.2) |
| zlib (`zlib1.dll`) | Not needed once FreeType is built from source, since FreeType carries its own inflate code. Otherwise it is another x86-only DLL. | — | — | — |
| OpenGL, GLU, Windows SDK libraries | System (SDK 10.0.26100.0) | — | yes | yes: run 3 found `OpenGL32.lib` and `GlU32.lib` under `um\arm64` |
| Vendored code compiled into `lt` (utfcpp, windirent, `GL.H`/`GLU.H`, kdtree, tri-tri, SMAA sizes) | As today: part of `lt`, with no project of its own | — | yes | yes (H3 caveat) |
| Unused vendored headers (enet, OVR, GLUT, GLUI, GLAux) | Copied with the tree; nothing builds them | — | — | — |

### 8.2 Dependencies without an ARM64 path

**FMOD Ex has none.** It was never built for ARM64, and it is closed source. The options, none of
which stubs or excludes it:

1. **ARM64EC** instead of ARM64 for that configuration. ARM64EC code can load and call the x64 FMOD
   Ex DLLs, and v145 is registered for ARM64EC on the build host (§2).
   - It is not the platform the brief names.
   - Every static library linked into it must be built as ARM64EC too. The source-built ones
     (SFML, FreeType, GLEW) can be.
   - FMOD then runs emulated.
2. **Port `Module/SoundEngine/Fmod.cpp` to FMOD Core/Studio 2.x**, which ships ARM64.
   - It is an API change, outside "migration only".
   - It changes x64 too.
   - It comes with its own licence terms.
3. **Accept ARM64 as a documented blocker** (the brief's Phase 4d). Everything builds for ARM64
   except the final links of `lt` and `launch`.
4. **The null engine the original already has.** `Module/SoundEngine/Null.cpp`
   (`SoundEngine_Null()`) is a working silent engine, and `src/old/ltheory/ltheory.cpp:91-93`
   chooses between it and FMOD at run time from the config flag `enableaudio`.
   - `launch.cpp:44` hard-wires `SoundEngine_Fmod()`.
   - `Fmod.cpp` is globbed into `lt.dll` on every platform.
   - So using the null engine on ARM64 still means leaving `Fmod.cpp` out of the ARM64 build. That
     is the "exclude" the brief forbids unless the owner says otherwise.

FreeType, GLEW and zlib reach ARM64 only through source builds or vcpkg (§8.1).

### 8.3 Other decisions needed before Phase 1 (all answered: §1, D6–D13)

1. **FMOD Ex x64 binaries.** Phase 2 cannot link x64 without them.
2. **R14 approval** to add GLEW source (Phase 2 cannot link x64 without an x64 GLEW either) and
   FreeType source (needed for ARM64), with their licence texts.
3. **D6**, the C++23 switch (§3.3).
4. **Copy scope and binaries in Git:**
   - **SFML.** Copy `src/`, `include/`, `extlibs/headers`, the Windows `extlibs/libs-msvc-universal`
     and `extlibs/bin`, and `license.md`. Leave out `examples/`, `doc/`, `tools/`, the CMake files
     (the brief excludes them), and about 50 MB of Android, iOS, macOS, MinGW and pre-2015 MSVC
     binaries no Windows build reads.
   - **ltheory.** Copy `src/` (including the unbuilt `src/old`), `include/`, `resource/`,
     `script/`, `LICENSE`, `README.md`, and `extlib/win32` plus `extbin/win32` (x86, kept as the
     original's although no FrontierOutpost platform can link them). Leave out `extbin/linux*` and
     `extbin/osx`.
   - **The root `.gitignore`** (`[Ww][Ii][Nn]32/`, `x64/`, `x86/`, `[Bb]in/`, `[Aa][Rr][Mm]64/`)
     would silently drop `extlib/win32`, SFML's `extlibs/libs-msvc-universal/{x86,x64}` and
     `extlibs/bin`, exactly as it did for `ltheory-old-main/`. It needs negation rules scoped to
     `FrontierOutpost/`.
   - **Runtime assets.** About 376 MB: sounds 252 MB, music 94 MB in two `.fsb` banks near
     GitHub's 100 MB per-file limit, fonts 29 MB. Either commit the LFS pointer files as
     `ltheory-old-main/` has them and let the smoke test fetch the real objects from upstream's LFS
     on the runner, or enable LFS here and commit the real files (quota, and someone able to
     fetch them must push).
   - **`resource.h`.** `FrontierOutpost/src/resource.h` needs a `-text` attribute (O2).
5. **Smoke test host.** The runner has no GPU; its OpenGL is the GDI 1.1 software implementation,
   so `launch.exe` will almost certainly fail at context creation or GLEW initialization. Either
   install a software GL (Mesa llvmpipe, a test-only tool) on the runner, or run the test on the
   owner's machine. An ARM64 smoke test needs an ARM64 host.

## 9. Phase 0d: baseline build of the original

The baseline is the ORIGINAL built as its author built it, through its own CMake:

- **Source:** upstream `0535d46` with SFML `192eb968` and the LFS objects for `extlib/win32` and
  `extbin/win32`. The runner fetched them from upstream's LFS without trouble.
- **Tools:** CMake 4.4.3 with `Visual Studio 18 2026`, toolset v145 (MSVC 14.51.36231), SDK
  10.0.26100.0.
- **Runs:**
  - Run 1 ([`36059516741`](https://github.com/Zwaliebaba/FrontierOutpost/actions/runs/36059516741))
    never configured because of a quoting bug in the harness (§10). It
    says nothing about the original.
  - Run 2 ([`36059952365`](https://github.com/Zwaliebaba/FrontierOutpost/actions/runs/36059952365))
    is the result below.
  - Run 3 re-measures with warning logs and import tables (§9.4).

### 9.1 Result

Configure succeeds for both architectures.

| Arch | Config | Result | Wall clock (build step) |
|---|---|---|---|
| **Win32** (the author's configuration) | Debug | **builds and links** | 5 min 18 s |
| **Win32** | Release | **builds and links** | 8 min 36 s |
| **x64** (the brief's baseline) | Debug | **fails at `lt.dll`'s link**: `LNK1120: 84 unresolved externals` | 5 min 11 s |
| **x64** | Release | same failure | 9 min 49 s |

**Why x64 fails.** Every translation unit of `lt`, `launch` and all six SFML libraries compiles for
x64, at the original standard, without an error. Only `lt.dll`'s link fails, and it fails on the
two libraries that have no x64 build anywhere in the tree:

- 56 GLEW symbols (`__glewActiveTexture` ... `__GLEW_VERSION_2_1`), from `glew32s.lib`;
- 28 FMOD Ex symbols (`FMOD_EventSystem_Create` and 27 C++ methods of `FMOD::System`, `Channel`,
  `Sound`, `Event`, `EventSystem` and `EventParameter`), from `fmodex_vc.lib` and
  `fmod_event.lib`.

The linker skips the x86 libraries. **No FreeType symbol is unresolved**: on x64, FreeType
references resolve from SFML's bundled x64 static FreeType 2.5.5. `launch.exe` is not built,
because it needs `lt`.

**Consequence.** The x64 baseline is "compiles completely, links nothing". The Win32 baseline is
the only complete reference, and Phase 2's parity comparisons (warnings, binary set) are made
against it.

### 9.2 Binaries produced

**Win32.** `bin/` is shared by both configurations, so Release overwrites Debug.

- `bin/lt.dll`: 14,842,880 bytes (Debug) / 9,021,440 (Release).
- `bin/lt.lib`: the import library. `bin/lt.exp`.
- `bin/lt.pdb`: Debug only. Release links without `/DEBUG` and leaves Debug's in place.
- `bin/launch.exe`: 231,936 / 126,976 bytes. `bin/launch.pdb` (Debug).
- `bin/fmod_event.dll`, `fmodex.dll`, `freetype6.dll`, `zlib1.dll`, copied by the post-build step.
- `build/ext/SFML/lib/<Config>/`:
  - `sfml-{system,window,network,graphics,audio}-s-d.lib` (Debug) and `-s.lib` (Release);
  - `sfml-main-d.lib` and `sfml-main.lib`;
  - compile PDBs `sfml-<module>-s[-d].pdb`.

**x64.** The same SFML libraries. `bin/` has only `lt.lib`, `lt.exp` and `lt.pdb`, written before
the failing link.

### 9.3 Effective settings (the generated projects)

Win32 and x64 are **identical apart from architecture names**, 404 settings compared.

| Setting | `lt` | `launch` | SFML (six libraries) |
|---|---|---|---|
| Configuration type | DynamicLibrary | Application | StaticLibrary |
| Character set | MultiByte (so `_MBCS`; `UNICODE` undefined, and Win32 calls bind to the `A` functions) | MultiByte | MultiByte, except **`sfml-window`: Unicode**. It defines `UNICODE;_UNICODE`, and CMake's generator sets the character set to match. (Phase 0 read this as MultiByte plus the definitions; corrected in Phase 1.) |
| Warning level | `/W3` | `/W3` | `/W3` |
| Exception handling | **`SyncCThrow` = `/EHs`**. The appended `/EHs` replaces CMake's `/EHsc`. | `/EHs` | `Sync` = `/EHsc` |
| Floating point | **`Fast`** | `Fast` | default (precise) |
| Instruction set | `StreamingSIMDExtensions2`, **passed on x64 too**, where it is valid and equals the default (no D9002 in any log) | same | not set |
| `/MP` | yes | yes | no |
| RTTI | on | on | on |
| Runtime library | `/MDd` / `/MD` | same | same |
| Debug | `/Od /Ob0 /RTC1 /Zi`; link `/DEBUG /INCREMENTAL` | same | `/Od /Ob0 /RTC1 /Zi` |
| Release | `/O2 /Ob2`, `NDEBUG`, no `/Zi`; link `/INCREMENTAL:NO`, no `/DEBUG` | same | `/O2 /Ob2`, `NDEBUG` |
| Definitions | `WIN32 _WINDOWS [NDEBUG] SFML_STATIC`, plus CMake's own `CMAKE_INTDIR="<cfg>"` and `lt_EXPORTS` | same, without `lt_EXPORTS` | `WIN32 _WINDOWS [NDEBUG] SFML_STATIC _CRT_SECURE_NO_DEPRECATE _SCL_SECURE_NO_WARNINGS`, plus `UNICODE _UNICODE` (window), `STBI_FAILURE_USERMSG` (graphics), `OV_EXCLUDE_STATIC_CALLBACKS FLAC__NO_DLL` (audio) |
| Include directories | `src/liblt; include; include/FreeType; ext/SFML/include` | same | `ext/SFML/include; ext/SFML/src`, plus `extlibs/headers/{stb_image,freetype2}` (graphics) or `extlibs/headers/AL; extlibs/headers` (audio) |
| Linker extras | `/NODEFAULTLIB:libcmt`; post-build copies the 4 DLLs | none (the `EXECUTABLES` bug, §5.4) | — |
| Subsystem | Console | Console | — |

`CMAKE_INTDIR` and `lt_EXPORTS` are **read by nothing** in ltheory or SFML. They are CMake
artefacts, and the MSBuild projects will not carry them (§10).

The generated projects also write some settings as **empty elements**, which leaves the tool's own
default in force rather than MSBuild's: `RemoveUnreferencedCodeData` (so no `/Zc:inline`),
`SupportJustMyCode`, `BufferSecurityCheck`, `CallingConvention`, `TreatWChar_tAsBuiltInType`,
`ForceConformanceInForLoopScope` and `MinimalRebuild`, and for the linker
`DataExecutionPrevention`, `RandomizedBaseAddress` and `ImageHasSafeExceptionHandlers`. They set
`UseFullPaths` and `ScanSourceForModuleDependencies` to `false`. §15.4 says how each carries over.

**`lt`'s link order**, identical in both configurations:

1. `sfml-graphics-s[-d]`, `sfml-network-s[-d]`, `sfml-system-s[-d]`, `sfml-window-s[-d]`;
2. `opengl32`, `glu32`;
3. `extlib/win32/FMOD/fmod_event.lib`, `FMOD/fmodex_vc.lib`, `Glew/glew32s.lib`,
   **`freetype/freetype.lib`** (the import library of `freetype6.dll`), `freetype/freetype28s.lib`;
4. `gdi32`, `opengl32`, **SFML's `extlibs/libs-msvc-universal/<arch>/freetype.lib`**,
   `sfml-system-s[-d]`, `winmm`, `ws2_32`;
5. CMake's defaults: `kernel32 user32 gdi32 winspool shell32 ole32 oleaut32 uuid comdlg32
   advapi32`.

`launch` links the SFML libraries, `opengl32`, `glu32`, `lt.lib`, then **all of `lt`'s link
libraries again**. A plain-signature `target_link_libraries` makes them transitive.

**Sources.** `lt`'s 382 match §5.6 exactly, as a set and in order. `launch` compiles
`src/launch/launch.cpp` and `src/resources.rc`. SFML compiles 102 files, relative to
`ext/SFML/src/SFML/`:

- `sfml-system` (16): System/Clock, System/Err, System/Lock, System/Mutex, System/Sleep, System/String, System/Thread, System/ThreadLocal, System/Time, System/FileInputStream, System/MemoryInputStream, System/Win32/ClockImpl, System/Win32/MutexImpl, System/Win32/SleepImpl, System/Win32/ThreadImpl, System/Win32/ThreadLocalImpl
- `sfml-main` (1): Main/MainWin32
- `sfml-window` (24): Window/Clipboard, Window/Context, Window/Cursor, Window/GlContext, Window/GlResource, Window/Joystick, Window/JoystickManager, Window/Keyboard, Window/Mouse, Window/Touch, Window/Sensor, Window/SensorManager, Window/VideoMode, Window/Window, Window/WindowImpl, Window/Win32/CursorImpl, Window/Win32/ClipboardImpl, Window/Win32/WglContext, Window/Win32/WglExtensions, Window/Win32/InputImpl, Window/Win32/JoystickImpl, Window/Win32/SensorImpl, Window/Win32/VideoModeImpl, Window/Win32/WindowImplWin32
- `sfml-network` (10): Network/Ftp, Network/Http, Network/IpAddress, Network/Packet, Network/Socket, Network/SocketSelector, Network/TcpListener, Network/TcpSocket, Network/UdpSocket, Network/Win32/SocketImpl
- `sfml-graphics` (31): Graphics/BlendMode, Graphics/Color, Graphics/Font, Graphics/Glsl, Graphics/GLCheck, Graphics/GLExtensions, Graphics/Image, Graphics/ImageLoader, Graphics/RenderStates, Graphics/RenderTexture, Graphics/RenderTarget, Graphics/RenderWindow, Graphics/Shader, Graphics/Texture, Graphics/TextureSaver, Graphics/Transform, Graphics/Transformable, Graphics/View, Graphics/Vertex, Graphics/GLLoader, Graphics/Shape, Graphics/CircleShape, Graphics/RectangleShape, Graphics/ConvexShape, Graphics/Sprite, Graphics/Text, Graphics/VertexArray, Graphics/VertexBuffer, Graphics/RenderTextureImpl, Graphics/RenderTextureImplFBO, Graphics/RenderTextureImplDefault
- `sfml-audio` (20): Audio/ALCheck, Audio/AlResource, Audio/AudioDevice, Audio/Listener, Audio/Music, Audio/Sound, Audio/SoundBuffer, Audio/SoundBufferRecorder, Audio/InputSoundFile, Audio/OutputSoundFile, Audio/SoundRecorder, Audio/SoundSource, Audio/SoundStream, Audio/SoundFileFactory, Audio/SoundFileReaderFlac, Audio/SoundFileReaderOgg, Audio/SoundFileReaderWav, Audio/SoundFileWriterFlac, Audio/SoundFileWriterOgg, Audio/SoundFileWriterWav

### 9.4 Warnings and runtime imports (run 3)

Run 3 ([`36063278543`](https://github.com/Zwaliebaba/FrontierOutpost/actions/runs/36063278543))
repeats run 2's result exactly. Wall clocks: Win32 Debug 4 min 56 s and Release 8 min 27 s; x64
5 min 21 s and 9 min 35 s, with the same 84 unresolved externals.

**Warnings and errors.** "Unique" collapses MSBuild's closing repeat of every diagnostic and a
header warning's repeat per translation unit; §14 O6 has the caveat on these numbers.

| Arch / config | MSBuild totals | Unique warnings by code | By project | Errors |
|---|---|---|---|---|
| Win32 Debug | 86 warnings, 0 errors | **229**: C4244 226, C4996 3 | `lt` 226, `sfml-network` 2, `sfml-system` 1 | 0 |
| Win32 Release | 86 warnings, 0 errors | 229, the same set | same | 0 |
| x64 Debug | 620 warnings, 113 errors | **594**: C4267 360, C4244 226, LNK4272 5, C4996 3 | `lt` 589, `sfml-network` 3, `sfml-graphics` 1, `sfml-system` 1 | 113 (LNK2001/LNK2019 on 84 symbols, and LNK1120) |
| x64 Release | 620 warnings, 113 errors | 594, the same set | same | 113 |

- **C4244** (conversion, possible loss of data) is the original's own warning load. It is
  identical on both architectures, and all of it is in `lt`.
- **C4267** (`size_t` narrowed to a smaller type) is what 64-bit adds: 360 sites, 358 of them
  in `lt` (2 in SFML).
- **LNK4272** is the linker refusing the five x86 libraries: `fmod_event`, `fmodex_vc`,
  `glew32s`, `freetype`, `freetype28s`.
- **Not one C4311, C4302 or C4312**, in any configuration. The compiler finds no pointer
  truncated to a smaller integer or widened from one (§7).

**The prebuilt DLLs' version resources:**

| DLL | Version | What |
|---|---|---|
| `fmodex.dll` | 4.44.14 | FMOD Ex Sound System |
| `fmod_event.dll` | **4.44.20** | FMOD Event System: a later point release than the core's headers and DLL |
| `freetype6.dll` | **2.3.5** (build 2742) | FreeType |
| `zlib1.dll` | 1.2.11 | zlib (its FileVersion field says 2.2.11) |

**What the built binaries load (Win32, `dumpbin /imports`).** The counts are functions per DLL:

| Binary | Imports |
|---|---|
| `lt.dll` | `OPENGL32` 56 (Release 39), `fmodex` 16, `fmod_event` 12, **`freetype6` 9**, `GDI32` 9/6, `WINMM` 5, `WS2_32` 2, `KERNEL32` 53/43, `USER32` 51/47, `SHELL32` 1, `ADVAPI32` 3, the VC++ runtime (`MSVCP140[D]`, `VCRUNTIME140[D]`, the UCRT) |
| `launch.exe` | `lt.dll` 24, `KERNEL32`, the VC++ runtime |

**So the original renders all its text with FreeType 2.3.5.** `liblt` calls eleven FreeType
functions, all in `LTE/Font.cpp`. `lt.dll` imports nine of them from `freetype6.dll`, the first
FreeType on its link line; the other two are not referenced by compiled code, since 2.3.5 exports
all eleven. SFML's font code is not linked into `lt.dll` at all: its `Font.cpp` alone calls 23
FreeType functions and none appear. So on Win32, `freetype28s.lib` and SFML's bundled 2.5.5
contribute nothing at run time. On x64 the same eleven references bind to SFML's static 2.5.5,
because the linker skips the x86 libraries.

## 10. Deviations from the original build

### 10.1 The baseline (Phase 0)

The baseline deviates from the author's recipe in four ways, none of which touches the original's
files:

- CMake 4.4.3 is given `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`. CMake 4 refuses
  `cmake_minimum_required(VERSION 3.0)` and 3.0.2 (SFML) otherwise.
- The source is a fresh clone, because the build writes into `${CMAKE_SOURCE_DIR}/bin`.
- It builds Debug and Release, as the brief asks; the author's `configure.py` builds
  RelWithDebInfo.
- It builds **x64** (the brief's baseline) **and Win32** (the author's `-A Win32`, the only
  configuration the original is known to build in).

Harness faults along the way, recorded so a failure is never mistaken for a property of the
original:

- **Run 1:** PowerShell split the unquoted `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` at the dot, so
  CMake read `3` plus a stray path `.5` and refused to configure. The argument is quoted since
  run 2.
- **Run 2:** `cmake --build` runs MSBuild from the build directory, so the relative `/flp` log path
  put the logs where the summary never looked. Build results were unaffected; the warning counts
  come from run 3.

### 10.2 The MSBuild projects (Phase 1)

Every setting in §9.3 carries over unless it is listed here, including the original's quirks:
`/EHs` rather than `/EHsc`, `/fp:fast`, `/arch:SSE2` on x64, and `/NODEFAULTLIB:libcmt` on `lt`
only. §15.4 gives the reasoning for each entry.

- **Output layout** (the brief's). Every output goes to `bin\<Platform>\<Configuration>\` and
  every intermediate to `obj\<Platform>\<Configuration>\<Project>\`. The original writes `lt` and
  `launch` to one `bin/` in the source tree that every configuration overwrites, and the SFML
  libraries to `build/ext/SFML/lib/<Config>/`.
- **No glob.** `lt`'s 382 sources are listed in `lt.vcxproj`, in the glob's order. A new source
  file is not built until it is added there and to the `.filters`.
- **Two definitions dropped:** `CMAKE_INTDIR="<cfg>"` and `lt_EXPORTS`. Nothing reads either.
- **`/FC` is passed**, which the original did not pass. CMake hands `cl` absolute source paths,
  and MSBuild hands it project-relative ones. With `/FC`, `__FILE__` (used by `src/liblt/Common.h`
  and `LTE/StackFrame.h`) stays an absolute path, as in the original.
- **Link lines keep the original's order but not its repetitions.** CMake repeats the libraries it
  collects transitively (`gdi32`, `opengl32` and `sfml-system` for `lt`; the SFML set for
  `launch`). MSVC's linker searches every library until nothing more resolves, so a repetition
  changes nothing. The Windows import libraries are CMake's default set; MSBuild's own default
  would add `odbc32` and `odbccp32`.
- **Dependency substitutions** (ADR-002):
  - `glew32s.lib` and `freetype.lib` come from this solution's `glew` and `freetype` projects;
  - `freetype28s.lib` and SFML's bundled `freetype.lib` are no longer linked;
  - FMOD Ex x64 is `fmod_event64.lib` and `fmodex64_vc.lib` from `extlib\x64\FMOD\` (D8);
  - ARM64 links no FMOD library (D9).
- **Post-build copy.** Every DLL in `extbin\<Platform>\` is copied next to `lt.dll` by MSBuild's
  `Copy` task with `SkipUnchangedFiles`, which is what `cmake -E copy_if_different` did. At x64
  that will be the FMOD Ex pair. `freetype6.dll` and `zlib1.dll` are no longer needed.
- **Two new projects**, `freetype` and `glew`, replace prebuilt x86 binaries (ADR-002). Their
  settings follow their upstream builds, not §9.3: FreeType's `vc2010` project compiles at `/W4`
  without C4001 and with `/Za`, except `ftdebug.c`; GLEW uses the shared defaults.
- **`launch` also references `freetype` and `glew`**, so that both are built before it links them.
- **Platforms:** x64 and ARM64 only, as the brief says. Neither the original's Win32 nor the
  deleted Visual Studio template's x86 (§15.2) is carried over. ARM64 gets no `/arch`.

## 11. Code changes

None. Phase 1 changes no source file.

## 12. BEHAVIOUR-RISK register

- **BR1: FreeType 2.5.5 replaces 2.3.5** (Phase 1). `lt` links `freetype.lib` from
  `ext/freetype/freetype.vcxproj` (`src/liblt/lt.vcxproj`, `AdditionalDependencies`). The Win32
  original draws its text with `freetype6.dll`, which is 2.3.5 (§9.4). FreeType 2.4 made the
  TrueType bytecode interpreter the default hinter, so glyph shapes and metrics may differ. In the
  original's x64 link, which fails only on GLEW and FMOD, the same references resolve to SFML's
  bundled 2.5.5 (§9.1). So x64 now matches the original's x64 in version, though not
  necessarily in build options.
- **BR2: GLEW built from 1.7.0 source replaces the prebuilt `glew32s.lib`** (Phase 1),
  `ext/glew/glew.vcxproj`. The vendored headers are 1.7.0's, so any difference lies in how the
  unknown prebuilt was built.

Candidates, to be logged with file:line when they happen:

- `fmod_event.dll` 4.44.20 against `fmodex.dll` 4.44.14 is the original's own pairing; the x64
  pair the owner supplies must be checked for the same (D8).
- Floating point on ARM64 (§7): `fmadd` contraction under `/fp:fast`, and float→int saturation.

## 13. Modernisation backlog

Recorded, not to be done in this migration:

- `src/liblt/Common.h:87-97` architecture detection (H1). Test `_WIN64` first, or drop it.
- `OS_GetUserDataPath()` returns `./cache/` relative to the working directory. AGENTS.md R13 wants
  a known location.
- `cmake/FindGLEW.cmake` is dead. So are the unused vendored headers (enet, OVR, GLUT, GLUI,
  GLAux) and the Linux and macOS binaries in `extbin/`.
- Three FreeType libraries and a fourth set of headers in one link (§5.3).
- `include/GL/GL.H`/`GLU.H` shadow the SDK headers.
- `offsetof` through null pointers (H9).
- FMOD Ex was discontinued in 2014. FMOD Core/Studio 2.x is the maintained line and has ARM64.

## 14. Open issues

- **O1** (§4) The copy in `ltheory-old-main/` is not the original. Its gaps are filled from
  upstream per D1. `ltheory-old-main/` itself stays as committed, incomplete.
- **O2** (§4) `.gitattributes:78` corrupts `resource.h` on checkout. **Resolved for the copy**
  (Phase 1): `FrontierOutpost/.gitattributes` stores and checks out `src/resource.h` byte for
  byte. `ltheory-old-main/` is still affected, and is read-only.
- **O3** The repository's `build.yml` has been red on `main` since `24db2b9` "New Setup". It still
  builds `Lockstep.slnx` and calls the deleted `Build/*.py`. Out of scope; flagged.
- **O4** ARM64 binaries can be built on `windows-latest` but not run. An ARM64 smoke test needs
  an ARM64 host (§8).
- **O5** The runtime assets are LFS objects. The build host can fetch them from upstream; this
  container cannot (the proxy serves no LFS).
- **O6** For Win32, MSBuild's own total (86 warnings) is *lower* than the de-duplicated count
  (229), which should be impossible if both count the same thing. This is unexplained.
  - The raw logs are in the run's `baseline-Win32` and `baseline-x64` artifacts, which this
    container cannot download: the proxy refuses the artifact host.
  - Phase 2 will compare **unique warnings by code, from the same script over both builds**,
    which is like for like whatever MSBuild's total means. It will also print raw counts to
    settle O6.
- **O7** This container can read job logs but not artifacts. The owner can download them from the
  run page.
- **O8** (D8) The owner's FMOD Ex x64 files are not in the tree yet. Until they are, x64's `lt`
  cannot link, so neither can `launch`, and Phase 2's gate cannot pass. Meanwhile the
  `frontieroutpost` job probes `lt`'s link without the FMOD libraries, to show whether anything
  else is missing (§15.6).
- **O9** (§15.1) `extlib/win32` and `extbin/win32` are not in the copy, contrary to the approved
  scope (§8.3 item 4, D11). Upstream keeps the 9 files (4.7 MB of x86 FMOD Ex, GLEW, FreeType and
  zlib binaries) in LFS; their pointer files cannot be pushed here (GitHub GH008), and the
  container cannot fetch the real files. No FrontierOutpost platform uses them. Owner's decision
  pending: leave them out, or have the real files committed.

## 15. Phase 1: structure

### 15.1 The copy (1a)

`FrontierOutpost/` holds a copy of the complete original (D1), not of `ltheory-old-main/`. It was
taken from Git blobs and release files rather than from checked-out working trees, and every
copied file is byte-identical to its source.

| Where | Files | From | What |
|---|---|---|---|
| `src/` | 742 | upstream `0535d46` | `liblt`, `launch`, `resources.rc`, `resource.h`, and the unbuilt `src/old` |
| `include/` | 122 | upstream | every vendored header, used or not (§6) |
| `resource/` | 634 | upstream | runtime assets, 284 of them LFS pointer files (D12) |
| `script/` | 6 | upstream | the author's manual generators and tools (§5.1) |
| `extlib/win32/`, `extbin/win32/` | **0** | — | **Not copied (O9).** Upstream stores these 9 x86 files in LFS and the container cannot fetch LFS objects (O5), so the copy would be LFS pointer files. GitHub refuses a push that adds pointers for LFS objects this repository does not hold (GH008). No FrontierOutpost platform can link or load x86 files. |
| `LICENSE`, `README.md` | 2 | upstream | |
| `ext/SFML/` | 483 | SFML `192eb968` | `src/` (every platform's sources, as upstream has them), `include/`, `extlibs/headers`, `extlibs/libs-msvc-universal/{x86,x64}`, `extlibs/bin/{x86,x64}`, `license.md`, `readme.md`, `changelog.md`, `CONTRIBUTING.md` |
| `ext/freetype/` | 461 | FreeType `VER-2-5-5` (`232bd948`) | `include/`; `src/` without the other build systems' files (`tools/`, `Jamfile`, `rules.mk`, `module.mk`); `builds/windows/ftdebug.c`; the licences `docs/FTL.TXT`, `docs/GPLv2.TXT` and `docs/LICENSE.TXT`; `README` |
| `ext/glew/` | 3 | the GLEW 1.7.0 release (ADR-002) | `src/glew.c`, `LICENSE.txt`, `README.txt` |

Left out, per D11 and the brief:

- every CMake file: ltheory's `CMakeLists.txt` and `cmake/`, SFML's, and FreeType's;
- `configure.py`, the author's CMake driver;
- ltheory's `.gitattributes`, `.gitignore` and `.gitmodules`;
- `extbin/linux32`, `linux64` and `osx`;
- for now, `extlib/win32` and `extbin/win32` (O9);
- SFML's `examples/`, `doc/`, `tools/`, and its prebuilt libraries for other platforms and for
  MSVC before 2015.

Two new files keep the copy intact in this repository:

- **`FrontierOutpost/.gitattributes`** stores `src/resource.h` byte for byte (O2). The copy holds
  the original 906-byte UTF-16LE blob.
- **`FrontierOutpost/.gitignore`** ignores `bin/`, `obj/`, `.vs/` and per-user files. It also
  overrides the root `.gitignore`'s `Win32`, `x64`, `x86`, `bin` and `ARM64` rules for the vendored
  folders that have those names. Without that, the 30 files of SFML's Windows implementation in
  `src/SFML/*/Win32/` would have been silently left out, as `extlib/win32` was left out of
  `ltheory-old-main/`. `extlib/x64` and `extbin/x64` are covered in advance for the owner's FMOD
  files (D8).

### 15.2 The Visual Studio template (a correction to Phase 0)

Phase 0 missed a Visual Studio desktop-application template that commit `6d8b20e` ("Sync") had
already put in place:

- `FrontierOutpost/FrontierOutpost.{cpp,h,rc,ico,vcxproj,vcxproj.filters}`;
- `Resource.h`, `framework.h`, `small.ico` and `targetver.h`;
- a root `FrontierOutpost.slnx` that listed it for ARM64, x64 and x86.

§12's Phase 0 statement that no FrontierOutpost code existed was therefore wrong. The owner decided
to delete the template (Phase 1), and `FrontierOutpost.slnx` was rewritten.

### 15.3 The solution and the projects (1b, 1c)

`FrontierOutpost.slnx`, at the repository root, has the build types Debug and Release, the
platforms x64 and ARM64, and ten projects in three solution folders:

| Folder | Project | File (under `FrontierOutpost/`) | Type | Output, Debug / Release | Replaces |
|---|---|---|---|---|---|
| `ltheory` | `lt` | `src/liblt/lt.vcxproj` | DLL | `lt.dll` and its import library `lt.lib` | the original's `lt` |
| `ltheory` | `launch` | `src/launch/launch.vcxproj` | console EXE | `launch.exe` | the original's `launch` |
| `SFML` | `sfml-system`, `sfml-window`, `sfml-network`, `sfml-graphics`, `sfml-audio` | `ext/SFML/src/SFML/<Module>/sfml-<module>.vcxproj` | static | `sfml-<module>-s-d.lib` / `sfml-<module>-s.lib` | SFML's own targets |
| `SFML` | `sfml-main` | `ext/SFML/src/SFML/Main/sfml-main.vcxproj` | static | `sfml-main-d.lib` / `sfml-main.lib` | SFML's own target |
| `dependencies` | `freetype` | `ext/freetype/freetype.vcxproj` | static | `freetype.lib` | the prebuilt `freetype.lib` with `freetype6.dll`, and `freetype28s.lib` |
| `dependencies` | `glew` | `ext/glew/glew.vcxproj` | static | `glew32s.lib` | the prebuilt `glew32s.lib` |

Each project sits in the folder of its sources. It has a `.filters` file that mirrors that
folder's structure, and it opens with a comment saying where its sources and settings come from.

**`FrontierOutpost/Directory.Build.props`** holds what every project shares:

- toolset `v145`, and the newest installed Windows SDK (`10.0`, which is 10.0.26100.0 on the host);
- MultiByte, and no whole-program optimisation;
- the output and intermediate directories;
- the compiler settings that are identical across all of the original's targets (§9.3): `/W3`,
  the language standard, `/GR`, no precompiled headers, `/EHsc` and `/fp:precise`;
- Debug's `/Od /Ob0 /RTC1 /Zi /MDd` and linker `/DEBUG /INCREMENTAL`, against Release's
  `/O2 /Ob2 /MD`, `NDEBUG` and `/INCREMENTAL:NO`.

Each project states only what differs from that. Every path is relative, through
`$(MSBuildThisFileDirectory)` (as `$(FrontierOutpostDir)`), `$(OutDir)` and `$(IntDir)`.

**The language standard during Phases 1 and 2** is `stdcpp14`, that is `/std:c++14`: MSVC's
default, and so what the original compiled as (§3.1). Phase 3 changes it for `lt` and `launch`.

**How the files were written.** The long lists (`lt`'s 382 sources and 340 headers, and the
`.filters` files) were typed by a throwaway script from the file lists in §5.6 and §9.3, and then
checked (§15.5). The script is neither in the repository nor part of the build. What it wrote is
plain MSBuild with relative paths and no generator artefacts, and it is maintained by hand from
here on, like everything else.

### 15.4 How the original's settings carry over

Most settings map one to one from §9.3. These did not:

- **Same-named sources.** 92 of `lt`'s sources share their file name with at least one other:
  43 names across 22 folders (six `Custom.cpp`, three `Camera.cpp`, and so on). With MSBuild's
  default `ObjectFileName=$(IntDir)`, their objects would overwrite one another (MSB8027). Each of
  them therefore compiles into an object folder named after its source folder. CMake's generator
  gives such sources explicit object names too, and the baseline raises no MSB8027. The other 290
  sources compile in one `/MP` batch.
- **`sfml-window`'s character set is Unicode** (§9.3). The other nine projects are MultiByte.
- **The empty elements** in the generated projects (§9.3) leave the tools' defaults in force.
  - MSBuild's default differs for `RemoveUnreferencedCodeData`: it would pass `/Zc:inline`.
    `Directory.Build.props` turns it off.
  - It also turns `SupportJustMyCode` off, which keeps `/JMC` out whatever MSBuild's default is,
    and sets `ScanSourceForModuleDependencies` to the original's `false`.
  - For the others (`/GS`, `/Gd`, `/Zc:wchar_t`, `/Zc:forScope`, `/Gm-`, `/NXCOMPAT` and
    `/DYNAMICBASE`), MSBuild's default and the tool's are the same.
- **`UseFullPaths` is not carried over:** `/FC` stays on (§10.2).
- **SFML's compile PDBs** are named as `SFML_GENERATE_PDB` names them: `sfml-<module>-s.pdb`,
  beside the library.
- **`launch`'s resource script** is compiled with `launch`'s include directories and definitions,
  plus `_DEBUG` in Debug and `NDEBUG` in Release, as CMake passed them to `rc.exe`. Its icon,
  `..\resource\texture\multi.ico`, is a real file rather than an LFS pointer, and it resolves
  from the `.rc` file's folder as before.
- **Project references do not link** (`LinkLibraryDependencies=false`). They order the build, and
  each link line is written out in the original's order, as CMake's generated projects do it.
- **FMOD Ex** is linked through two properties, `FmodLibraries` and `FmodLibraryDir`, which are
  set for x64 only. ARM64 therefore links no FMOD library, and fails on the FMOD symbols. That is
  D9's blocker, showing as exactly what it is.

### 15.5 What was checked in the container

The container has no MSVC (§2), so these checks are static:

- every project, `.filters` file, `Directory.Build.props` and the solution are well-formed XML;
- every source, header and resource item exists at its relative path;
- every project reference points at a project whose GUID matches;
- each `.filters` file lists exactly its project's items, and defines every filter it uses;
- `lt`'s 382 object paths are unique, ignoring case;
- the item counts equal the original's:
  - `lt`: 382 sources, and 340 headers;
  - `launch`: 1 source and 1 resource script;
  - SFML: 102 sources;
  - SFML headers, against the generated projects' own counts: System 30, Window 34, Network 13,
    Graphics 38, Audio 24, Main 0;
- no build file contains a drive letter, `CMake`, `ZERO_CHECK` or `ALL_BUILD`, and there is no
  CMake file anywhere under `FrontierOutpost/`.

Whether it builds is for the host to say (§15.6).

### 15.6 The build on the host

The workflow's new `frontieroutpost` job builds `FrontierOutpost.slnx` from a clean checkout. It
finds MSBuild through `vswhere` and runs
`msbuild FrontierOutpost.slnx /m /p:Configuration=<cfg> /p:Platform=x64`, one job per
configuration; ARM64 joins in Phase 4. The job:

- fails when the build fails;
- lists the outputs with their machine types, and what `lt.dll` and `launch.exe` import;
- summarises the diagnostics with the same script as the baseline;
- prints every project's compiler, librarian, linker and resource-compiler command line
  (`.github/migration/CommandLines.py`). The baseline job now prints the original's as well, so
  that Phase 2 compares the two builds switch by switch rather than only by their project files;
- while the FMOD x64 files are missing (O8), links `lt` once more without the FMOD libraries and
  reports what is still unresolved.

Results: pending.
