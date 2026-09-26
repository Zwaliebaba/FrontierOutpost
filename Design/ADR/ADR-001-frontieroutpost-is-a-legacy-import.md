# ADR-001: FrontierOutpost is a legacy import, exempt from the conformance rules

- **Status:** Accepted (owner, 2026-09-24, at the start of the ltheory-old migration)
- **Scope:** `FrontierOutpost.slnx` and everything under `FrontierOutpost/`, and, since ADR-004,
  `GameData/`: the original's runtime data, moved out of `FrontierOutpost/`. Nothing else. The
  solution builds `NeuronClient/` and `Tests/NeuronClientTests/` from the NeuronClient plan's
  Phase 2; they are outside the exemption, and AGENTS.md governs them in full (ADR-005). So is
  `FrontierOutpost/src/liblt/Shaders/`, from that plan's Phase 4: liblt's shaders are new code (its
  N11, ADR-008), so AGENTS.md's rules for shaders and the checkers apply to them, in `lt.vcxproj`.
  The rest of `lt` stays exempt.
- **Detail:** `Design/Archive/MIGRATION_NOTES.md`

## Context

`FrontierOutpost/` receives a full copy of `ltheory-old`: about 720 C++ files written from 2012 to
2015, plus SFML 2.5.0 and other vendored code. The migration moves that copy from CMake to
hand-authored MSBuild, then to C++23, then to ARM64. The owner's brief makes it a build-system and
language-standard migration only. It forbids renaming namespaces, identifiers, classes, files or
user-facing strings. It requires a parity build at the original's standard before any bump. It
forbids getting code to compile by deleting it or by suppressing warnings wholesale. And it targets
x64 **and ARM64**.

AGENTS.md cannot be met by such a tree without rewriting it:

- **§1:** the naming table, gated by clang-tidy. The original uses its own conventions throughout,
  such as `Window_Create`, `m`-less members and lower-case file names under `src/old`. Conforming
  means renaming, which the brief forbids.
- **§2:** flat project directories. `src/liblt` alone has twelve subdirectories, some nested
  (`Audio/Signal`, `Game/Action`, `LTE/Type`), and the brief requires the directory layout to be
  preserved.
- **§3:**
  - "x64 is the only platform". The brief targets ARM64 as well.
  - `/W4` with warnings as errors, on code the original built at `/W3` with its own pragma
    set.
  - `/std:c++latest` from the start. The brief builds at the original standard first, and
    `/std:c++latest` is C++23 plus draft C++26 on this toolset (MIGRATION_NOTES.md §3.3).
- **§4 and `.clang-format`:** reformatting the tree would bury every migration change in churn.
- **R16:** `/fp:precise` stated explicitly. The original compiles with `/fp:fast` (and
  `/arch:SSE2` on x86), and parity means keeping what it had.
- **R13 and R14:** runtime files and dependencies are ADR decisions. The original writes
  everything under `./cache/`, relative to the working directory, and links SFML, FreeType, GLEW
  and FMOD Ex. The files are inherited unchanged. How each dependency is supplied is decided at
  the migration's Checkpoint 0.

## Decision

`FrontierOutpost/` is a **legacy import**. For it:

1. **§1, §2 and §4 do not apply.** Names, layout and formatting stay as in the original.
2. **§3 and R16 apply only as the brief amends them:**
   - platforms are x64 and ARM64;
   - the warning level and the floating-point model are the original's (`/W3`, `/fp:fast`);
   - `/arch` follows the original. It passed `/arch:SSE2`, which only means something on x86, a
     platform FrontierOutpost does not build;
   - the language standard moves from the original's (C++14, MSVC's default) to C++23 in the
     brief's Phase 3;
   - dependencies may keep their own standard.

   Toolset v145, building through the solution, and relative paths **do** apply.
   Debug and Release differ exactly as the original's configurations do, and in nothing else.
   That covers optimisation, inlining, runtime checks, the CRT variant, and debug information,
   which the original emits only in Debug. It is wider than §3 allows, because parity requires
   it.
3. **R13:** the original's runtime files are inherited as they are and listed in
   MIGRATION_NOTES.md §5.8, not redesigned.
4. **R14:** the original's dependencies are inherited. How each is supplied (vendored source,
   prebuilt, or a package manager) is recorded in its own ADR once the owner approves the
   Checkpoint 0 plan. No dependency beyond those is added without the same approval.
5. **The working rules in §6 apply unchanged:** stay in scope, record decisions, report what
   was actually verified.

The exemption covers the migrated code only. New first-party code written outside
`FrontierOutpost/` is governed by AGENTS.md in full.

## What this forecloses

- Running `.clang-tidy`, `.clang-format` or a future `Build/CheckProjectFiles.py` as gates over
  `FrontierOutpost/`. Any such checker must exclude it until a later ADR narrows this one, as
  ADR-008 does for `src/liblt/Shaders/`.
- Renaming, flattening or reformatting the imported code as part of the migration. That work, if
  wanted, is its own decision afterwards.
- Reading "x64 is the only platform" as forbidding the ARM64 configurations of
  `FrontierOutpost.slnx`.
