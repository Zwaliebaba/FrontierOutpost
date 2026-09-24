# AGENTS.md — Engineering Rules

Operating instructions for every agent (and human) writing code in this repository. **Read this before generating a single line.**

This file is about **how code is written here**: naming, layout, build settings and the standing rules of the codebase. It assumes a C++23 codebase built on Windows with MSVC. It is not the design. What the product *is* belongs in a design document, and what it is built on (graphics API, audio, networking, platform layers) is decided per project and recorded as an ADR (§6).

**What is authoritative, in order:**

1. **This file**: conformance, meaning naming, style, build settings, and how to work here.
2. **`Design/ADR/`**: engineering decisions taken while building, one file per decision (§6). Numbering starts at `ADR-001` in each repository and does not continue another's.
3. **The surrounding code**: for anything neither of the above covers, match the file you are editing.

A design document, when there is one, sits alongside rather than above: it says what is built and this file says how. Where no design authority exists, a task that needs a design answer asks the owner and gets the answer written down before the code is.

If a rule here conflicts with a habit from another codebase, this file wins. If you think a rule is wrong or your task cannot be done without deviating, **say so in your report. Never deviate silently.**

---

## 1. Naming convention (normative, no exceptions)

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `RenderTarget` |
| Function, method | `PascalCase` | `PresentFrame()` |
| Member variable | `m_camelCase` | `m_deviceLost` |
| Static member (mutable) | `sm_camelCase` | `sm_activeDevice` |
| Global | `g_camelCase` | `g_instance`, `g_frameCount` |
| Parameter | `_camelCase` | `_fileName`, `_entityId` |
| Local | `camelCase` | `shadedColor` |
| Compile-time constant | `UPPER_CASE` | `WIDTH_PIXELS`, `TICKS_PER_SECOND` |
| Enumerator | `PascalCase` | `DeviceLost`, `OutOfMemory` |
| Macro | `UPPER_CASE` | `PROJECT_ASSERT` |
| Namespace | `PascalCase` | `Engine`, `Game` |
| File | `PascalCase.cpp` / `.h` | `RenderTarget.cpp` |

**Note the split that catches people out: a `constexpr` is `UPPER_CASE`, an enumerator is `PascalCase`.** They are both compile-time and they are spelled differently on purpose. An enumerator is a *value of a type* and reads as one at the use site (`LoadFault::OutOfMemory`), while a constant is a number with a name and is meant to look like one. [`.clang-tidy`](.clang-tidy) enforces both and is the single source of truth for the option values. This document states the rules in prose and does not repeat the settings, so there is nothing to drift.

### The rules behind the table

**R1: The leading underscore on parameters is deliberate.** It is legal C++. The reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form: no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2: A type name carries no prefix or affix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else. This bans `CFoo`, `SFoo`, `EFoo`, `IFoo`, `FooBase`, `FooAbstract`, `FooImpl` and `_t` suffixes. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept
├── UdpTransport      ← a socket-backed one
└── LoopbackTransport ← in-process, for tests
```

A base class for one derived class is ceremony: name the concept, and add the layer when a second thing needs it.

clang-tidy can require an *absent* prefix but cannot see a *present* suffix, so the repository checker carries the other half (§6).

**R3: Compile-time constants are `UPPER_CASE`.** `constexpr`, `inline constexpr` and `static constexpr` members: `WIDTH_PIXELS`, `TICKS_PER_SECOND`. `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4: Acronyms capitalize as words**: `HlslSource`, `GpuBuffer`, `UdpTransport`, never `HLSLSource`. Identifiers from an external SDK keep that SDK's spelling (`HRESULT`, `HANDLE`, `ID3D12Device`, `VkInstance`, `GLuint`) and are never renamed to fit.

**R5: Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6: Units belong in names; types do not.** `durationMs`, `widthPixels`, `arrivalTick`, `confidencePercent` are encouraged. Unit ambiguity is a real defect class and one the compiler cannot catch for you. Never encode the type: no `iCount`, `pEntity`, `strName`.

**R7: A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc` and `.inl` are not used; template implementations live in the header. Exceptions, because MSBuild and the Visual Studio wizards spell them this way: `pch.h`, `pch.cpp`, `framework.h`, `targetver.h`, `Resource.h`.

**R8: `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes private members `m_`. A public aggregate (a `Desc` config struct, a wire record, a POD handed to another subsystem) uses plain `camelCase` fields so brace initialization reads naturally.

**R9: One namespace per layer, and lower layers do not know higher ones.** Reusable engine or library code gets its own namespace; application code gets another. The split is a rule rather than a filing preference: if an engine type has to know an application concept by name to do its job, it is in the wrong layer. Test suites use `namespace <Project>Tests`.

**R10: No `using namespace` at file scope in a header.** It leaks into every translation unit that includes it, and the failure it causes appears somewhere else. In a `.cpp` it is allowed for the unit-test framework and nothing else; otherwise qualify the name or write a local alias.

**R11: One spelling per family, and it is the SDK's (American).** `color`, `initialize`, `serialize`, `normalize`, `quantize`, `synchronize`, `behavior`, `neighbor`, `center`, `gray`, `canceled`. Neither spelling is wrong English; the defect is a tree where a reader has to know which half they are in and a grep for one finds half the uses. The platform SDKs spell it American, so that half wins. Prose is not checked; an identifier is.

### Worked example: this is the target style

```cpp
// Engine/SaveFile.h
#pragma once

#include <cstdint>

namespace Engine
{

// R3: constant → UPPER_CASE. R6: the unit is in the name.
inline constexpr std::uint32_t MAX_SAVE_BYTES = 16u * 1024u * 1024u;

// Enumerator → PascalCase, unlike the constant above.
enum class SaveFault : std::uint8_t
{
  NotFound,
  Corrupt,
  TooLarge
};

/// A save file opened for reading or writing.
/// R2: no prefix on the type. R8: private state carries m_.
class SaveFile
{
public:
  struct Desc                                            // R8: aggregate → plain fields
  {
    const wchar_t* path;                                 // R17: a string you do not write is const
    std::uint32_t maxBytes;                              // R6: unit in the name
  };

  [[nodiscard]] static bool Open(const Desc& _desc,      // R1: _ on parameters
                                 SaveFile& _outFile) noexcept;

  [[nodiscard]] std::uint32_t SizeBytes() const noexcept { return m_sizeBytes; }

private:
  HANDLE m_file = nullptr;                               // R4: SDK spelling kept as-is
  std::uint32_t m_sizeBytes = 0;
  bool m_readOnly = true;
};

} // namespace Engine
```

### Enforcement

| Rule | Enforced by |
|---|---|
| The naming table, R1, R3, R5, R8 | [`.clang-tidy`](.clang-tidy), gated in CI over the whole tree |
| R2 affixes, R7 file names and project registration, R11 spellings, §2 flat directories | `Build/CheckProjectFiles.py`, gated in CI |
| R4, R6, R9, R10 | Review. Check your own diff against the table before handing it back. |

Until a checker exists, the rules in its row are review's problem and nothing else. A rule nobody can run is a rule that rots, so writing the checkers is early work rather than housekeeping.

---

## 2. Repository shape

The concrete layout (the solution, the projects and the edges between them) is settled when the first project is created and recorded in an ADR at that point. These are the standing constraints any layout has to satisfy.

**Project directories are flat, with exactly two sanctioned subdirectories.** C++ source lives directly in its project's folder. This is not taste: `.clang-tidy`'s `HeaderFilterRegex` matches headers exactly one level in, so **a header in a subdirectory is silently unchecked**, with no findings, no warning, and nobody noticing for months. The two exceptions are the shader pipeline, whatever shading language and compiler the project chose:

- **`<Project>/Shaders/`** holds hand-written shader source. Name each file for the shader and its stage (for example `<Shader>VS.hlsl` / `<Shader>PS.hlsl`, or `<Shader>.vert` / `<Shader>.frag`); the convention is set by the ADR that picks the graphics stack and then applied consistently.
- **`<Project>/CompiledShaders/`** holds what the shader compiler wrote. It is **build output**: produced by a build step in the `.vcxproj` on every build, listed in `.gitignore`, skipped by every checker, and never edited or committed.

**The edges run one way, and a layer never reaches sideways.** Engine code is built on by application code and never the reverse (R9), and two libraries at the same level share what is below them rather than each other. An edge that only exists "for now" is an edge, and it is the one that will be impossible to remove later.

**The project files are part of the source.** Adding, removing or moving a file means editing the owning `.vcxproj` **and** its `.filters`. A file that compiles locally but is missing from the project fails only in CI, or worse, links a stale object nobody notices.

**Dependencies are deliberate.** See R14.

**Build and IDE output is never committed**: `x64/`, `.vs/`, `*.user`, and anything a build step generates.

---

## 3. Build and verify

**x64 is the only platform.** No Win32/x86 configuration in any project or solution; do not add one, and do not write code that only works at 32 bits.

**The compiler settings are the settings.** Toolset `v145` (Visual Studio 2026), `/std:c++latest`, `/permissive-`, `/W4` with **warnings as errors**, `/fp:precise`, and an explicit `/arch` (R16). There is no CMake. If a build error tempts you to change the toolset, lower the language standard, turn off `/permissive-` or silence a warning, **stop and report instead.**

**Debug and Release are aligned by rule, not by luck.** Every setting that is not *about* optimisation reads identically in both configurations: language standard, conformance, warning level, include directories, precompiled header, floating-point model, instruction set. The two differ in exactly four things: `Optimization`, `_DEBUG` vs `NDEBUG`, `FunctionLevelLinking`/`IntrinsicFunctions`, and the linker's folding and LTCG switches. (MSBuild spells those four through a few more properties, namely `UseDebugLibraries`, `RuntimeLibrary` as the debug or release CRT, `LinkIncremental`, `WholeProgramOptimization`, `EnableCOMDATFolding` and `OptimizeReferences`, and that list is the whole of what may differ.)

That alignment matters more than it looks, because **CI builds Debug only** (§6). A Release that quietly lost an include directory or sat on an older language standard would not be discovered until someone ships. A static check of the two configurations is what stands in for the build nobody runs.

**Build through the solution, never a `.vcxproj` directly.** Output paths and cross-project include directories are anchored on `$(SolutionDir)`, and MSBuild defines `SolutionDir` only for a solution build. Building a project file directly resolves every one of those paths against the *project* folder instead of the repository root. **It does not fail, and that is the problem.** Output lands in the wrong folder, so the next solution build links against whichever copy is staler, and every cross-project include path becomes a directory that does not exist. To build one project, use `/t:<ProjectName>` on the solution.

```powershell
# Everything, from the repository root, naming the solution.
msbuild <Solution>.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo

# One project, still through the solution.
msbuild <Solution>.slnx /t:<ProjectName> /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Release, before you claim anything about it.
msbuild <Solution>.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

**A project does not put its own directory on the include path.** `cl.exe` already searches the directory of the including file first for a quoted include. Only the directories of *other* projects are listed, as `$(SolutionDir)<Project>`.

**Run the tests**, through `vstest.console.exe`, over every suite the build produced.

**vstest reports "no tests found" as a pass.** An empty suite is therefore worse than no suite: it is a green check mark over a library nobody exercised. Every test project ships a placeholder `SuiteSmoke` for exactly this reason; delete it when the first real test lands, never before.

**Run the checkers before you push.** They are seconds of Python and they are what CI runs:

```powershell
python Build\CheckFormat.py           # clang-format, whole tree. --fix rewrites the offenders
python Build\CheckProjectFiles.py     # build shape, project registration, R2/R7/R11
python Build\RunClangTidy.py          # needs a Developer PowerShell (INCLUDE must be set)
```

**A green build says nothing about whether the program draws.** For anything touching rendering, input, audio or presentation, launch the executable and look at it.

**Report what you actually did.** "Builds clean, not run" and "builds and runs" are different claims. Never imply the second when you only did the first, and say which configurations you built.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority for C++ layout. [`.editorconfig`](.editorconfig) covers everything clang-format does not (line endings, encoding, final newline, trailing whitespace, and the non-C++ formats) and repeats the indent and column numbers an editor needs before the first save.

**The tree is formatted, and CI keeps it that way.** Format what you write; if the check fires, run `--fix` and commit the result rather than arguing with it.

- **Do not reformat what your task did not touch.** A drive-by reformat produces pure churn and buries your actual change.
- **Include order is load-bearing and grouped by hand**, which is why `SortIncludes` is `Never`: `pch.h`, then `<windows.h>` before any other platform or graphics SDK header, then the rest of the SDKs, then project headers, then the standard library. A formatter reordering these behind a change's back is a correctness risk, not a style preference.
- **One header owns the Windows macro family, and nothing else defines any of it.** Whatever set the project chooses (`NOMINMAX`, `WIN32_LEAN_AND_MEAN` and so on) is set in that one header, before `<windows.h>`, and the project files define none of them. Two owners of one macro is C4005, and `/WX` makes that fatal: `/D` spells a bare macro as `1` where a `#define` spells it as nothing, so the collision is guaranteed rather than possible. If you need `<windows.h>`, include that header; do not add the macros yourself.
- Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it.

---

## 5. Rules for this codebase

**R12: Graphics is an open choice, recorded as a decision.** No graphics API, presentation model or rendering technique is mandated or excluded by this file. Direct3D 12, Direct3D 11, Vulkan, OpenGL, a software rasterizer, or several of them behind an abstraction are all legitimate, as are any resolution strategy, window style, multisampling, HDR, post-processing or presentation scheme. The choice is made by the project, recorded in an ADR, and applied consistently from then on. What holds regardless of the choice:

- **GPU and COM object lifetimes are RAII from the first line.** Use the appropriate owner (`Microsoft::WRL::ComPtr` for COM, a small owning wrapper for Vulkan/GL handles). A raw `AddRef`/`Release` pair or an unpaired create/destroy in new code is a defect, not a style.
- **Graphics code lives in its own layer** (R9). Application code talks to it through the project's own types, so a later change of API or technique touches that layer and not everything above it.
- **Anything that is a policy rather than a mechanism** (how the frame reaches the window, how it scales, what resolution content is authored at) lives in one place and is written down in the ADR, so it is not re-decided pass by pass.

**R13: Runtime files are decisions.** Every file the executable reads at startup or writes while running (config, saves, logs, caches, assets on disk) is named in an ADR with its form and lifetime. A file the program *creates* is a different thing from a file it *requires in order to start*, and the ADR says which. A path the program writes resolves against a known location (beside the executable, or a documented user-data folder), never against the working directory: a log written relative to the launch directory silently goes somewhere nobody looks.

**R14: Dependencies are deliberate.** The baseline is the Windows SDK and the MSVC standard library. Any other library, SDK, package or package manager (graphics helpers, shader compilers, audio, physics, UI, anything from NuGet, vcpkg or GitHub) is a decision: propose it with what it buys and what it costs, record it in an ADR, and add it only once it is approved. Never add one silently to get a task done.

**It binds what the executable is built from, not what a development tool needs.** Scripts under `Build/` and `Tools/` never ship and never link, so a baker that needs a Python package does not reopen this rule. **Third-party *content* compiled in or shipped (art, fonts, audio, shaders) is the owner's call**: anything under a licence needs the owner's approval before it lands, with the licence text travelling with the bytes.

**R15: Memory is plain C++.** `new`/`delete` where it must be, RAII everywhere, standard containers by default. No pool, slab or free-list allocator without a decision recorded in `Design/ADR/`.

**R16: Floating point and instruction set are stated, not inherited.** Every project states `/fp:` and `/arch:` explicitly in the project file, identically in Debug and Release. A default is not a decision, and the symptom of losing one is two builds of the same code disagreeing about the same sum with no line to blame. The chosen `/arch` sets a CPU floor (for example, AVX2 requires Intel Haswell or AMD Excavator; an older CPU meets an illegal instruction, not a message), and it may let MSVC contract `a*b+c` into an FMA even under `/fp:precise`, which changes float results and may differ between optimisation levels. Record the choice and its floor in an ADR.

**Where a component must be deterministic** (a simulation that is replayed, a lockstep network model, anything reproduced from a seed), additionally: no `float` where a fixed-point or integer quantity will do (hold a fraction as integer hundredths and say so in the name, R6), no iteration over an unordered container whose order reaches the outcome, and **no wall-clock time inside it: the tick is the clock.** Wall time maps to ticks at the seam, and that is the only place the two meet. Randomness is a pinned PRNG with an explicit seed, never `std::random_device`, never a hash of an address. If floats must enter a deterministic component, that is an ADR, not a workaround.

**R17: A string you do not write is `const`.** `/permissive-` turns on `/Zc:strictStrings`: a literal is `const char[N]` and will not bind to `char*`. The fix is `const` on the signature, never a cast at the call site. A `const_cast` here is a lie about a literal that lives in a read-only section, and writing through it is a real crash rather than a theoretical one.

**R18 and up are reserved** for conformance rules that come from a project's design (which state a routine may read, what an event must carry, where tuning values live). Write them here as R18 onward when there is a design to cite, without renumbering anything above. Until then, do not invent one and do not import one from another tree: a rule with no source behind it is a rule nobody can settle an argument with.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent code that offends you is not part of the task: note it in your report and move on. Unrequested "while I was in there" changes are the main way a tree acquires regressions it cannot bisect.

**Record decisions as ADRs.** An engineering decision (a graphics stack, a file format, a wire protocol, a subsystem's shape, a new dependency, an exception to a rule here) goes in `Design/ADR/` as one file per decision, numbered in order from `ADR-001-<slug>.md`, stating the context, the decision and what it forecloses, in the same commit as the change that implements it. Figures in an ADR are measured, not estimated; if you quote one, say how you measured it. A decision nobody wrote down gets re-litigated every few months by whoever forgot it.

**Write the checkers early.** `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py` are what §1, §2 and §3 lean on. Until each one lands, the rules it would enforce are review's problem.

**What CI runs.** The CI workflow under `.github/workflows/` checks the build shape, builds **Debug|x64**, runs the test suites and clang-tidy, and checks formatting on a pinned clang-format. **Every step that has something to run blocks; a step whose input does not exist yet is skipped, not faked.** Each gate is guarded on the file it needs (the checker script, the solution, the built test DLLs), so the workflow starts gating the moment that file lands. The guards are the only concession: nothing is `continue-on-error`, and a script that exists and fails still fails the build. Remove a guard once its input is permanently there, and never add one to get past a red build.

**CI does not build Release.** What stands in for it is the static alignment check on the two configurations (§3) and, before a release, an actual `Configuration=Release` build by whoever is shipping. If you change something that could plausibly break only under optimisation, build Release yourself and say so.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. One change per PR. CI must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1: `_` on parameters, `m_` on class state, `UPPER_CASE` constants, `PascalCase` enumerators, no `I`/`C`/`Base` affixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New, removed or moved files are in the `.vcxproj` **and** the `.filters` of every project involved.
- [ ] No project's `ConformanceMode`, `LanguageStandard`, `WarningLevel` or `TreatWarningAsError` was changed, and no warning was silenced with a pragma.
- [ ] Debug and Release still agree on everything §3 says they must.
- [ ] No unapproved dependency (R14) and no undocumented runtime file (R13).
- [ ] The checkers pass, or, for one not yet written, the report says which and why.
- [ ] It builds Debug|x64, and every test suite runs and passes.
- [ ] If it touches rendering, input, audio or presentation: it was **run**, not just built.
- [ ] `Design/ADR/` has a new file if the change *was* a decision.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
