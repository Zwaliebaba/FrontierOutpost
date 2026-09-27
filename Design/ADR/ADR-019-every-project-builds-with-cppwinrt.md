# ADR-019: Every project builds with C++/WinRT

- **Status:** Accepted (owner, 2026-09-27)
- **Scope:** the `packages.config` and `.vcxproj` of all seven projects (NeuronCore, NeuronClient,
  NeuronServer, GameLogic, FrontierOutpost, NeuronClientTests and GameLogicTests),
  `Build/CheckProjectFiles.py`'s `APPROVED_PACKAGES` and `Build/RunClangTidy.py`'s include path,
  and every COM object NeuronClient and its tests hold
- **Amends:** ADR-005 point 6, as ADR-018 did; ADR-018 decision 1 ("NeuronClient's one NuGet
  package"); and AGENTS.md R12's owner of a COM object

## Context

The owner added Microsoft's C++/WinRT NuGet package to every project on 2026-09-27 (`a3009f2`), and
requires it. It reaches the Windows Runtime's APIs from standard C++, through headers its compiler,
`cppwinrt.exe`, generates at build time from the Windows SDK's metadata: no language extension, and
no runtime of its own. The owner wants it first for its base library: `winrt::com_ptr` in place of
`Microsoft::WRL::ComPtr`, and `winrt::check_hresult`.

`check_hresult` is not adopted here, because it would amend ADR-007's failure path. It throws, where
`GraphicsCore::Check` names the failed call through `onFailure`, reports a removed device once with
what DRED recorded, and returns so that the caller can refuse and go on, as the tests count. A throw
from a `noexcept` function ends the program without any of that.

`Build/CheckProjectFiles.py` refused the package, as R14 has it refuse any package no ADR approves,
and CI stopped at that check, before the build.

## Decision

1. **C++/WinRT 3.0.260818.1** (MIT, the latest on nuget.org on 2026-09-27) is named in every
   project's `packages.config`, and restored into `packages/` with WinPixEventRuntime (ADR-018), by
   the same command and the same CI step.
2. **The package's own `.props` and `.targets` set it up,** imported by each `.vcxproj`. ADR-018
   decision 2 states WinPixEventRuntime's settings in the project instead, so that the checkers read
   them. That is not done here, because generating the projection is the package's work: its
   targets run `cppwinrt.exe` over the SDK's metadata on each build, and restating them would copy a
   tool's internals into every project. Read from the package's build files on 2026-09-27, what they
   add is:
   - `/bigobj` on every compile, in every configuration;
   - `$(IntDir)Generated Files\` on the include path, where the projection is written. Here that is
     under `x64\<Configuration>\<Project>\` at the repository root: build output, which `.gitignore`
     leaves out and the checkers never list;
   - `WindowsApp.lib` on the link of the executable and the test suites;
   - a check that the Windows SDK is 10.0.17134 or later;
   - C++17 as the language standard, and `/await:strict` under C++17, only where a project states no
     standard. Every project states one (AGENTS.md §3), so neither applies.

   `Build/RunClangTidy.py` adds the project's `Generated Files` folder to its include path when a
   build has made it, so that clang-tidy reads the headers the build compiled, not the SDK's older
   copy of them.
3. **`APPROVED_PACKAGES` names it at that version,** with this ADR. Another version is a new
   decision (ADR-018 decision 6), and is read again for what its build files change.
4. **`winrt::com_ptr` owns every COM object** (owner, 2026-09-27), in place of
   `Microsoft::WRL::ComPtr`, and AGENTS.md R12 names it. The two precompiled headers include
   `<unknwn.h>` and then `<winrt/base.h>` where they included `<wrl/client.h>`: `base.h` supports
   classic COM interfaces, as Direct3D's are, only once `IUnknown` is declared. The move changes no
   behavior, by these rules:
   - `Get()` is `get()`, and `Reset()` is `= nullptr`.
   - An out parameter is `put()`, and `IID_PPV_ARGS(&p)` stays. At this version both release what
     the pointer held first, as WRL's `operator&` did. An older C++/WinRT asserts instead that the
     pointer is empty, so a new version is read again for this too (decision 3).
   - `As` is `QueryInterface` through `IID_PPV_ARGS`, which returns the same HRESULT. `as` throws,
     so it is not used; `try_as` may be, where no HRESULT is reported.
   - An array a call reads is built from `get()`, never from `put()`, which would release what it
     points to.

## Runtime file (R13)

None. The projection is headers, and `WindowsApp.lib` binds to Windows' own DLLs, so nothing ships
beside the executable.

## What this forecloses

- **Build settings the checkers read in full.** Decision 2's additions apply alike in Debug and
  Release, but only a build shows them.
- **A build with no network on a clean checkout,** as ADR-018 already has it, now for two packages.

## Verification

Not built on 2026-09-27 by the session that wrote this, which had no Windows: CI is the build.
`Build/CheckProjectFiles.py` passes with the package approved, and `Build/TestCheckers.py` passes.
With the package, CI built Debug|x64 and passed all 129 tests, at `2b4d35f` and again with decision
4 at `fd4ee88`. Its build step took 4:49 at `f884ca3`, before the package, and 5:19 and 4:13 at
those two, after it: one run each on GitHub's `windows-latest` runner, as the run's step times
give them. The runs after the package differ from each other by more than either differs from the
one before, so what the projection adds does not show at that resolution.

Decision 4 was checked with mingw-w64 against a stand-in for `base.h` that follows `com_ptr` at
this version's tag, with no `operator&` and a `put()` that releases first. Every NeuronClient and
test source compiles against it at `-Wall -Wextra`, but for errors in headers the Linux harness
lacks or has in another form, and `AudioDevice.cpp`, which needs `x3daudio.h` and was read
instead.
