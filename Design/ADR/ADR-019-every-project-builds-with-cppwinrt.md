# ADR-019: Every project builds with C++/WinRT

- **Status:** Accepted (owner, 2026-09-27)
- **Scope:** the `packages.config` and `.vcxproj` of all seven projects (NeuronCore, NeuronClient,
  NeuronServer, GameLogic, FrontierOutpost, NeuronClientTests and GameLogicTests), and
  `Build/CheckProjectFiles.py`'s `APPROVED_PACKAGES`
- **Amends:** ADR-005 point 6, as ADR-018 did, and ADR-018 decision 1 ("NeuronClient's one NuGet
  package")

## Context

The owner added Microsoft's C++/WinRT NuGet package to every project on 2026-09-27 (`a3009f2`), and
requires it. It reaches the Windows Runtime's APIs from standard C++, through headers its compiler,
`cppwinrt.exe`, generates at build time from the Windows SDK's metadata: no language extension, and
no runtime of its own. The owner wants it first for its base library: `winrt::com_ptr` in place of
`Microsoft::WRL::ComPtr`, and `winrt::check_hresult`. No code includes one of its headers yet.

Using them is the next change, and it amends two things this ADR does not:
- AGENTS.md R12, which names `Microsoft::WRL::ComPtr` as the owner of a COM object;
- ADR-007's failure path. `check_hresult` throws, where `GraphicsCore::Check` names the failed call
  through `onFailure`, reports a removed device once with what DRED recorded, and returns so that
  the caller can refuse and go on, as the tests count. A throw from a `noexcept` function ends the
  program without any of that.

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
3. **`APPROVED_PACKAGES` names it at that version,** with this ADR. Another version is a new
   decision (ADR-018 decision 6), and is read again for what its build files change.

## Runtime file (R13)

None. The projection is headers, and `WindowsApp.lib` binds to Windows' own DLLs, so nothing ships
beside the executable.

## What this forecloses

- **Build settings the checkers read in full.** Decision 2's additions apply alike in Debug and
  Release, but only a build shows them.
- **A build with no network on a clean checkout,** as ADR-018 already has it, now for two packages.

## Verification

Not built on 2026-09-27 by the session that wrote this, which had no Windows: CI's next build is the
first. `Build/CheckProjectFiles.py` passes with the package approved, and `Build/TestCheckers.py`
passes. The time the projection adds to a build is not measured.
