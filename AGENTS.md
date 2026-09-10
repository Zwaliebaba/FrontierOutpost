# AGENTS.md — Engineering Rules for *Frontier Outpost*

Operating instructions for every agent (and human) writing code in this repository. **Read this before generating a single line.**

*Frontier Outpost* is a greenfield C++23 space MMO: a Direct3D 12 client and an authoritative server, hosted in **one executable**, presenting a fixed **1280×720 R8G8B8A8** screen, drawn straight into the swap chain's back buffer and presented 1:1. There is no legacy tree here and nothing is grandfathered. A rule below is not a target to migrate towards; it describes the code as it must be written today, and a whole-tree run of any checker comes back clean.

**What is authoritative, in order:**

1. **This file** — conformance: naming, style, build, and how to work here.
2. **[Design/README.md](Design/README.md)** — design: what the target is, how a design decision is recorded, and what a design session owes the tree. Read the part your task touches before you start.
3. **The surrounding code** — for anything neither covers, match the file you are editing.

If a rule here conflicts with a habit from another codebase, this file wins. If you think a rule is wrong or your task cannot be done without deviating, **say so in your report — never deviate silently.**

---

## 1. Naming convention (normative — no exceptions)

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `SwapChainTarget` |
| Function, method | `PascalCase` | `PresentFrame()` |
| Member variable | `m_camelCase` | `m_deviceRemoved` |
| Static member (mutable) | `sm_camelCase` | `sm_activeDevice` |
| Global | `g_camelCase` | `g_instance`, `g_frameCount` |
| Parameter | `_camelCase` | `_fileName`, `_shipId` |
| Local | `camelCase` | `shadedColor` |
| Compile-time constant | `UPPER_CASE` | `WIDTH_PIXELS`, `GLYPH_SCALE` |
| Enumerator | `PascalCase` | `DeviceLost`, `OutOfVideoMemory` |
| Macro | `UPPER_CASE` | `FRONTIER_ASSERT` |
| Namespace | `PascalCase` | `Neuron`, `Frontier` |
| File | `PascalCase.cpp` / `.h` | `SwapChainTarget.cpp` |

**Note the split that catches people out: a `constexpr` is `UPPER_CASE`, an enumerator is `PascalCase`.** They are both compile-time and they are spelled differently on purpose — an enumerator is a *value of a type* and reads as one at the use site (`PageFault::OutOfVideoMemory`), while a constant is a number with a name and is meant to look like one. [`.clang-tidy`](.clang-tidy) enforces both, and it is the single source of truth for the option values; this document does not repeat them, so there is nothing to drift.

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form — no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix or affix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else. This bans `CFoo`, `SFoo`, `EFoo`, `IFoo`, `FooBase`, `FooAbstract`, `FooImpl` and `_t` suffixes. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept
├── UdpTransport      ← a socket-backed one
└── LoopbackTransport ← in-process, for tests
```

That tree is an illustration of the rule, not a description of anything. A base class for one derived class is ceremony: name the concept, and add the layer when a second thing needs it.

clang-tidy can require an *absent* prefix but cannot see a *present* suffix, so `Build/CheckProjectFiles.py` carries the other half.

**R3 — Compile-time constants are `UPPER_CASE`.** `constexpr`, `inline constexpr` and `static constexpr` members: `WIDTH_PIXELS`, `TICKS_PER_SECOND`, `GLYPH_SCALE`. `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `HlslSource`, `DxgiFactory`, `UdpTransport` — never `HLSLSource`. Identifiers from an external SDK keep that SDK's spelling (`ID3D12Device`, `DXGI_FORMAT`, `HRESULT`, `IDXGISwapChain4`) and are never renamed to fit.

**R5 — Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6 — Units belong in names; types do not.** `rangeMetres`, `durationTicks`, `speedUnitsPerTick` are encouraged — a simulation measured in world units and server ticks makes unit ambiguity a real defect class. Never encode the type: no `iCount`, `pShip`, `strName`.

**R7 — A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc` and `.inl` are not used; template implementations live in the header. Exceptions, because MSBuild and the Visual Studio wizards spell them this way: `pch.h`, `pch.cpp`, `framework.h`, `targetver.h`, `Resource.h`.

Adding, removing or moving a file means editing the owning `.vcxproj` **and** its `.filters`. `Build/CheckProjectFiles.py` checks both halves and the on-disk spelling.

**R8 — `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes private members `m_`. A public aggregate — a `Desc` config struct, a wire record, a POD handed to the renderer — uses plain `camelCase` fields so brace initialization reads naturally.

**R9 — One namespace per layer.** Engine code (`NeuronCore`, `NeuronClient`, `NeuronServer`) is `namespace Neuron`. Game code (`GameLogic`, and the game half of the executable) is `namespace Frontier`. The engine knows nothing about this game; if a type needs to know what a mining laser is, it is in the wrong library. Test suites use `namespace <Project>Tests`.

**R10 — No `using namespace` at file scope in a header.** It leaks into every translation unit that includes it, and the failure it causes appears somewhere else. In a `.cpp` it is allowed for the unit-test framework and nothing else; otherwise qualify the name or write a local alias.

**R11 — One spelling per family, and it is the SDK's.** `color`, `initialize`, `serialize`, `normalize`, `quantize`, `synchronize`, `behavior`, `neighbor`, `center`, `gray`, `canceled`. Neither spelling is wrong English; the defect is a tree where a reader has to know which half they are in and a grep for one finds half the uses. `D3D12_CLEAR_VALUE::Color` settles which half wins. Comments and prose are not checked; identifiers are, by `Build/CheckProjectFiles.py`.

### Worked example — this is the target style

```cpp
// NeuronClient/SceneTarget.h
#pragma once

#include <cstdint>

namespace Neuron
{

// R3: constant → UPPER_CASE. R6: the unit is in the name.
inline constexpr std::uint32_t SCREEN_WIDTH_PIXELS = 1280;
inline constexpr std::uint32_t SCREEN_HEIGHT_PIXELS = 720;

// R1 (enumerator) → PascalCase, unlike the constants above.
enum class TargetFault : std::uint8_t
{
  DeviceRemoved,
  BadFormat,
  OutOfVideoMemory
};

/// The 1280x720 color framebuffer the game draws into, and the depth buffer that goes with it.
/// R2: no prefix on the type. R8: private state carries m_.
class SceneTarget
{
public:
  struct Desc                                            // R8: aggregate → plain fields
  {
    std::uint32_t widthPixels;                           // R6: unit in the name
    std::uint32_t heightPixels;
    DXGI_FORMAT colorFormat;                             // R4: SDK spelling kept as-is
  };

  [[nodiscard]] static bool Create(ID3D12Device* _device,        // R1: _ on parameters
                                   const Desc& _desc,
                                   SceneTarget& _outTarget) noexcept;

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept { return m_widthPixels; }

private:
  ID3D12Resource* m_depthTarget = nullptr;
  std::uint32_t m_widthPixels = 0;
  bool m_deviceRemoved = false;
};

} // namespace Neuron
```

### Enforcement

| Rule | Enforced by |
|---|---|
| The naming table, R1, R3, R5, R8 | [`.clang-tidy`](.clang-tidy), gated in CI over the whole tree |
| R2 affixes, R7 file names and project registration, R11 spellings, §3 flat directories | `Build/CheckProjectFiles.py`, gated in CI |
| R4, R6, R9, R10 | Review. Check your own diff against the table before handing it back. |

---

## 2. Repository map

| Path | What it is | May you edit it? |
|---|---|---|
| `NeuronCore/` | Engine static library used by **both** halves: platform, timing, maths, containers, serialization, the wire protocol. **Currently holds only `Debug.h` and the umbrella header** — the MVP-01 protocol and transport were removed by ADR-015 and the 4X's have not been written | Yes |
| `NeuronClient/` | Engine static library used by the **client only**: the window, the D3D12 device and swap chain, the 1280×720 colour target, input, audio, UI | Yes |
| `NeuronServer/` | Engine static library used by the **server only**: session ownership, replication, the authoritative loop. **Currently empty** — ADR-015 removed the MVP-01 session; the project and its suite are where the 4X's authoritative loop goes | Yes |
| `GameLogic/` | The game itself — entities, orders, economy, simulation rules. Server-side; the client never links it directly. **Currently empty** — ADR-015 removed the MVP-01 ship simulation and the 4X's has not been written | Yes |
| `FrontierOutpost/` | The executable, and the main page it draws (`MainPage`, `MatchState`, the fixture). Where every embedded asset and compiled shader ends up. The server half is not wired up | Yes |
| `Tests/NeuronCoreTests/`, `Tests/NeuronClientTests/`, `Tests/NeuronServerTests/`, `Tests/GameLogicTests/` | MSVC CppUnitTest DLLs, one per library, each referencing the library it tests and the libraries that library is built on. **CI builds and runs all four** | Yes |
| `Design/` | The design record: `README.md` (the standards), `ADR/` (decisions), and plans | Yes — see §6 |
| `Build/*.py` | Repository checkers (§6). They gate CI | Yes, carefully |
| `.clang-format`, `.clang-tidy`, `.editorconfig` | Layout and naming, machine-readable (§1, §4) | Yes — with an owner decision |
| `.github/workflows/build.yml` | CI. All of it blocks | Yes, carefully |
| `x64/`, `.vs/`, `*.user` | Build and IDE output | **No — and never commit them** |

**Nine projects, and the edges run one way.** `FrontierOutpost.slnx` is the solution; its only platform is `x64`.

```
NeuronCore.lib          ← the engine everything else builds on
├── NeuronClient.lib    ← references NeuronCore
├── NeuronServer.lib    ← references NeuronCore
├── GameLogic.lib       ← references NeuronCore
└── FrontierOutpost.exe ← references all four

NeuronCoreTests.dll     ← NeuronCore
NeuronClientTests.dll   ← NeuronClient, NeuronCore
NeuronServerTests.dll   ← NeuronServer, NeuronCore
GameLogicTests.dll      ← GameLogic, NeuronCore
```

**`GameLogic` is referenced by the executable and by nothing else.** It is server-side game code; the day a client-side file reaches for it is the day the server stopped being authoritative. Likewise nothing in `NeuronClient` may reach `NeuronServer` or the reverse — they share `NeuronCore` and that is the whole of their common ground.

**Project directories are flat, with exactly two sanctioned subdirectories.** C++ source lives directly in `NeuronCore/`, `GameLogic/` and so on. This is not taste: `.clang-tidy`'s `HeaderFilterRegex` matches headers exactly one level in, so a header in a subdirectory is silently unchecked. `Build/CheckProjectFiles.py` fails the build on one. The two exceptions are the shader pipeline (owner decision, 2026-09-09):

- **`<Library>/Shaders/`** holds the HLSL, hand-written, named `<Shader>VS.hlsl` and `<Shader>PS.hlsl` for the vertex and pixel halves of one shader.
- **`<Library>/CompiledShaders/`** holds what the compiler wrote: one header per `.hlsl`, `<Shader>VS.h` and `<Shader>PS.h`, each declaring a byte array `g_<Shader>VS` / `g_<Shader>PS`. It is **build output** — produced by an `FXCompile` item in the `.vcxproj` on every build, listed in `.gitignore`, skipped by every checker, and never edited or committed. The `.cpp` that binds the pipeline state includes it and nothing else does.

**There are no vendored SDKs and no package manager.** The build depends on the Windows SDK and the MSVC standard library, and on nothing else. See R14.

---

## 3. Build and verify

**x64 is the only platform.** The Win32/x86 configurations were deleted from every `.vcxproj` and from the `.slnx` on 2026-09-09; do not restore them, and do not write code that only works at 32 bits. Toolset `v145` (Visual Studio 2026), `/std:c++latest`, `/permissive-`, `/W4` with **warnings as errors**, and there is no CMake. If a build error tempts you to change the toolset, lower the language standard, turn off `/permissive-` or silence a warning — stop and report instead.

**Debug and Release are aligned by rule, not by luck.** Every setting that is not *about* optimisation reads identically in both configurations: language standard, conformance, warning level, include directories, precompiled header, floating-point model. The two differ in exactly four things — `Optimization`, `_DEBUG` vs `NDEBUG`, `FunctionLevelLinking`/`IntrinsicFunctions`, and the linker's folding and LTCG switches. `Build/CheckProjectFiles.py` fails the build when anything else drifts apart.

That check matters more than it looks, because **CI builds Debug only** (§6). Release is compiled by whoever ships, and a Release that quietly lost an include directory or sat on an older language standard would not be discovered until then. The static check is what stands in for the build nobody runs.

```powershell
# Build everything: the executable, the four libraries it references, and the four test DLLs.
msbuild FrontierOutpost.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo

# Just the game and its libraries, still through the solution.
msbuild FrontierOutpost.slnx /t:FrontierOutpost /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Release, before you claim anything about it.
msbuild FrontierOutpost.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

All commands run from the repository root, and all of them name the **solution**.

**Build through `FrontierOutpost.slnx`, never a `.vcxproj` directly.** Output paths and cross-project include directories are anchored on `$(SolutionDir)`, and MSBuild defines `SolutionDir` only for a solution build. `msbuild NeuronCore\NeuronCore.vcxproj` therefore resolves every one of those paths against the *project* folder instead of the repository root. **It does not fail — that is the problem.** Output lands in `NeuronCore\x64\Debug\` instead of `x64\Debug\`, so the next solution build links against whichever copy is staler, and `$(SolutionDir)NeuronCore` becomes a path relative to the project that does not exist. The include breakage is latent: it bites the first time a file reaches across projects, which for a fresh test suite may be weeks after someone got into the habit. To build one project, use `/t:<ProjectName>` on the solution, as above. Everything lands in `x64\Debug\` (or `x64\Release\`) at the repository root; intermediates stay in each project's own `x64\` folder, which is `IntDir`'s default base.

**A project does not put its own directory on the include path.** `cl.exe` already searches the directory of the including file first for a quoted include, so `#include "FileSys.h"` from `NeuronCore\FileSys.cpp` resolves without help. Only the directories of *other* projects are listed, as `$(SolutionDir)<Project>`.

**Run the tests.** All four suites, through `vstest.console.exe`:

```powershell
vstest.console.exe x64\Debug\NeuronCoreTests.dll x64\Debug\NeuronClientTests.dll `
                   x64\Debug\NeuronServerTests.dll x64\Debug\GameLogicTests.dll /Platform:x64
```

**vstest reports "no tests found" as a pass.** An empty suite is therefore worse than no suite: it is a green check mark over a library nobody exercised. Each project ships a placeholder `SuiteSmoke` for exactly this reason; delete it when the first real test lands, never before.

**Run the checkers before you push.** They are seconds of Python and they are what CI runs:

```powershell
python Build\CheckFormat.py           # clang-format, whole tree. --fix rewrites the offenders
python Build\CheckProjectFiles.py     # build shape, project registration, R2/R7/R11
python Build\RunClangTidy.py          # needs a Developer PowerShell (INCLUDE must be set)
```

**A green build says nothing about whether the game draws.** For anything touching rendering, input, audio or presentation, launch it:

```powershell
x64\Debug\FrontierOutpost.exe
```

**Report what you actually did.** "Builds clean, not run" and "builds and runs" are different claims. Never imply the second when you only did the first, and say which configurations you built.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority for C++ layout: 2-space indent, 140 columns, Allman braces, pointer and reference bound left, includes never reordered. [`.editorconfig`](.editorconfig) covers everything clang-format does not — CRLF, UTF-8, final newline, trailing whitespace, and the non-C++ formats — and repeats the two numbers an editor needs before the first save.

**This tree is formatted, and CI keeps it that way.** Unlike a migration repository, a whole-tree `CheckFormat.py` run here is a no-op. Format what you write; if the check fires, run `--fix` and commit the result rather than arguing with it.

- **Do not reformat what your task did not touch.** The check being green tree-wide means a drive-by reformat produces pure churn and buries your actual change.
- **Include order is load-bearing and grouped by hand**, which is why `SortIncludes` is `Never`: `pch.h`, then `<windows.h>` before any D3D12/DXGI/XAudio2 header, then the rest of the SDK, then project headers, then the standard library. A formatter reordering these behind a change's back is a correctness risk, not a style preference.
- **`NeuronCore.h` owns the Windows macro family, and nothing else defines any of it.** `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `NODRAWTEXT`, `NOGDI`, `NOBITMAP`, `NOMCX`, `NOSERVICE`, `NOHELP` are set there, before `<windows.h>`, and the `.vcxproj` files deliberately define none of them. Two owners of one macro is C4005, and `/WX` makes that fatal — `/D` spells a bare macro as `1` where a `#define` spells it as nothing, so the collision is guaranteed rather than possible. If you need `<windows.h>`, include `NeuronCore.h`; do not add the macros yourself.
- **`NOGDI` means GDI is genuinely gone**, not discouraged. `GetStockObject`, `TextOut` and their kin are not declared. That is the point: the swap chain owns every pixel, and there is no case in this game where a GDI call is the right answer.
- Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it.

---

## 5. C++ rules for this codebase

**R12 — Graphics is Direct3D 12 only**, and the screen it presents is fixed. **1280×720 `R8G8B8A8_UNORM`** — not `_SRGB`, so a channel authored as `0xAA` is presented as `0xAA` — drawn straight into the swap chain's back buffer, whose client area is those same 1280×720 pixels. There is no intermediate render target, no resolve pass and no present scale (ADR-011). No D3D11, no D3D11On12, no immediate-mode helper layers. COM lifetimes are RAII from the first line — a raw `AddRef`/`Release` pair in new code is a defect, not a style.

**There is no sampler object anywhere in this renderer, and adding one is a decision.** The font atlas is read with `Texture2D<uint>::Load()`, which takes integer texel coordinates and has no filtering to switch on; the starfield is a hash of an integer pixel; the meshes carry no textures. Likewise `D3D12Defaults.h` turns blending, multisampling and anti-aliased lines off for every pipeline built from the shared defaults. Until ADR-011 those were *impossible* — the render target held palette indices and a blend of two of them was an unrelated colour. They are now conventions, which means a pass that wants one has to say so: **blending, multisampling or a sampler in a new pass is an ADR, not a pipeline field.**

**R13 — The executable ships alone.** There is no assets folder, no data directory, nothing beside `FrontierOutpost.exe` at runtime. Art, colours, fonts, meshes and sound are embedded as `constexpr` arrays in headers — `NeuronClient/Font.h` is the pattern: 96 glyphs, 8×8, one bit a pixel, 768 bytes, and nothing to load. **Shaders are compiled at build time**, never at runtime: `<Library>/Shaders/<Shader>VS.hlsl` goes through the `.vcxproj`'s `FXCompile` step into `<Library>/CompiledShaders/<Shader>VS.h` as `g_<Shader>VS` (§2). No `D3DCompile`, no `d3dcompiler_47.dll` beside the executable, no `.cso` on disk. Never add a runtime file dependency, a working-directory assumption or a "just for development" loose-file path; the loose path is the one that ships.

**R14 — No third-party dependencies and no package manager.** The Windows SDK and the MSVC standard library, and nothing else. If you believe something is unavoidable, propose it in your report with what it buys and what it costs — do not add it. This is a closed list, not a high bar.

**R15 — Memory is plain C++.** `new`/`delete` where it must be, RAII everywhere, standard containers by default. No pool, slab or free-list allocator without an owner decision recorded in `Design/ADR/`.

**R16 — Determinism is a property of the server, and it is built, not hoped for.** Every project compiles `/fp:precise` with no `/arch`, stated explicitly in the `.vcxproj` rather than inherited from an MSVC default — a default is not a decision, and the symptom of losing one is two builds of the same simulation disagreeing about the same sum with no line to blame. In `GameLogic`, additionally: no `float` where a fixed-point or integer quantity will do, no iteration over an unordered container whose order reaches the simulation, and no wall-clock time — the tick is the clock.

**R17 — A string you do not write is `const`.** `/permissive-` turns on `/Zc:strictStrings`: a literal is `const char[N]` and will not bind to `char*`. The fix is `const` on the signature, never a cast at the call site — a `const_cast` here is a lie about a literal that lives in a read-only section, and writing through it is a real crash rather than a theoretical one.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent code that offends you is not part of the task — note it in your report and move on. Unrequested "while I was in there" changes are the main way a young tree acquires regressions it cannot bisect.

**Keep the design record true.** If your change completes, alters or invalidates something in `Design/`, update that document in the same commit, and add an ADR when the change *is* a decision. [Design/README.md](Design/README.md) says which is which. Figures in design documents are measured, not estimated — if you quote a new one, say how you measured it.

**Keep the project files honest.** Adding, removing or moving a source file means editing the owning `.vcxproj` **and** its `.filters`. A file that compiles locally but is missing from the project fails only in CI — or worse, links a stale object nobody notices. `python Build\CheckProjectFiles.py` is the cheapest way to catch a half-done move.

**What CI runs, and what it does not.** [`.github/workflows/build.yml`](.github/workflows/build.yml) has two jobs, and every step of both blocks:

| Job | Steps |
|---|---|
| **Windows** | `CheckProjectFiles.py` → build **Debug\|x64** → build the four test DLLs → `vstest.console.exe` over all four → `RunClangTidy.py` over the whole tree |
| **Linux** | `CheckFormat.py` on clang-format 18.1.3 |

**CI does not build Release** (owner decision, 2026-09-09). The Windows build is the slow half of the pipeline and a second configuration roughly doubles it for a tree where the two differ only in optimisation. What stands in for it is the static alignment check in `CheckProjectFiles.py` (§3) — and, before a release, an actual `Configuration=Release` build by whoever is shipping. If you change something that could plausibly break only under optimisation, build Release yourself and say so.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. CI must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1 — `_` on parameters, `m_` on class state, `UPPER_CASE` constants, `PascalCase` enumerators, no `I`/`C`/`Base` affixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New, removed or moved files are in the `.vcxproj` **and** the `.filters` of every project involved.
- [ ] No project's `ConformanceMode`, `LanguageStandard`, `WarningLevel` or `TreatWarningAsError` was changed, and no warning was silenced with a pragma.
- [ ] Debug and Release still agree on everything §3 says they must.
- [ ] `python Build\CheckFormat.py`, `python Build\CheckProjectFiles.py` and `python Build\RunClangTidy.py` pass.
- [ ] It builds Debug|x64, and the four suites run and pass.
- [ ] If it touches rendering, input, audio or presentation: it was **run**, not just built.
- [ ] `Design/` updated if the change moved a decision, and an ADR added if the change *was* one.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
