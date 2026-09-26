# ADR-006: NeuronClient builds for x64 and ARM64, with lt's instruction sets

- **Status:** Accepted (owner, 2026-09-25, at the start of the NeuronClient migration's Phase 2).
  Amended (owner, 2026-09-26, the plan's N18): decision 5's temporary workflow is gone.
- **Scope:** the platforms, `/arch` and `/fp` of `NeuronClient/` and `Tests/NeuronClientTests/`
- **Detail:** `Design/Archive/NeuronClient-migration.md` §3 (point 4), §5.1, §6 (the rule, Phase 0
  step 2, Phase 2 step 1), §7, §10, N1, N5 and N10. This amends AGENTS.md §3 for these two
  projects, and is the record of the `/arch` choice that AGENTS.md R16 asks for.

## Context

AGENTS.md §3 makes x64 the only platform. ADR-001 amends that for the migrated code, and
`FrontierOutpost.slnx` builds `lt` and `launch` for x64 and ARM64. NeuronClient is linked into
`lt.dll` (ADR-005), so it has to build wherever `lt` does, but it is outside ADR-001's exemption.
The owner keeps ARM64 (N1).

AGENTS.md R16 asks every project to state `/fp:` and `/arch:` explicitly, the same in Debug and
Release, and to record the `/arch` choice and the CPU floor it sets. `lt` states `/arch:SSE2` on
x64 and nothing on ARM64 (`FrontierOutpost/src/liblt/lt.vcxproj`), and `/fp:fast` on both.

The owner first chose AVX2 on x64 (N5). But NeuronClient shares `lt.dll` with liblt. A template or
inline function that both use, every `std::vector` and `std::string` member among them, is compiled
into each and kept once, as whichever copy the linker picks. With NeuronClient at AVX2, liblt could
run AVX2 instructions. `lt.dll`'s static initialisers run before `launch.exe`'s `main()`, so no
check in `main()` could come first, and a CPU without AVX2 could crash before any message. AVX2
buys little in a layer over Direct3D 12, DirectWrite, WIC and XAudio2, and the CPU-heavy code is
liblt's. The owner dropped it (N10).

ARM64 cannot be verified in CI. No runner executes it, and ARM64 GPU drivers see the least
Direct3D 12 use (plan §3, point 4).

## Decision

1. **NeuronClient and NeuronClientTests build for x64 and ARM64, in Debug and Release** (N1). This
   amends AGENTS.md §3 for these two projects only. The rest of §3 applies to them unchanged, and
   Win32/x86 stays excluded.
2. **`/arch` is `lt`'s** (N10), the same in Debug and Release. On x64 the project states
   `StreamingSIMDExtensions2` (`/arch:SSE2`). On ARM64 it states `NotSet`, which passes no `/arch`
   and so keeps the compiler's default (N5); the toolset is pinned at v145 (AGENTS.md §3), so that
   default moves only with a deliberate toolset change. No binary mixes `/arch`, and the game's CPU
   floor is the platform's own.
3. **`/fp:precise`,** as AGENTS.md §3 has it, where `lt` is at `/fp:fast`. A copy of a shared
   template may round as either library would. Nothing relies on bit-exact results (plan §10,
   `Design/Archive/MIGRATION_NOTES.md` BR6).
4. **`Build/CheckProjectFiles.py` enforces both:** ARM64 for these two projects and no other, and
   each platform's `/arch` as decision 2 states it (plan Phase 0 step 2).
5. **All four builds pass after every migration step** (plan §6). CI builds Debug|x64. The other
   three are built by a temporary workflow for the length of the migration, or by hand. The
   workflow went on 2026-09-26 (N18); its last run was on `8443028`. From then, the owner builds
   the three by hand, before a release and in the plan's Phase 5 checks, not after every step.
6. **Every run-time claim about ARM64 waits for the owner's ARM64 device** (plan §3 point 4, §7).
   That includes the test that loads `d3dcompiler_47.dll` and reflects a compiled blob (ADR-008).

## What this forecloses

- **An instruction set above `lt`'s for NeuronClient,** AVX2 among them, unless `lt` moves with
  it under a new ADR. A binary that mixes `/arch` can run the higher set anywhere in it.
- **Changing `/arch` or `/fp` on either platform without a new ADR,** or letting Debug and Release
  differ in either (AGENTS.md R16).
- **Win32/x86 for NeuronClient.** The amendment adds ARM64 and nothing else.
- **ARM64 as something CI can vouch for.** CI proves that Debug|x64 compiles and links, and that
  the tests pass there (plan §7).
