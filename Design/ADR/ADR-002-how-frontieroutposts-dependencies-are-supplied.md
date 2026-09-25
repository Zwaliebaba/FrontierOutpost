# ADR-002: How FrontierOutpost's dependencies are supplied

- **Status:** Accepted (owner, 2026-09-25, at the migration's Checkpoint 0)
- **Scope:** `FrontierOutpost/`
- **Detail:** `FrontierOutpost/MIGRATION_NOTES.md` §6, §8 and §9.4. This implements ADR-001's
  decision 4 and AGENTS.md R14.

## Context

The original `ltheory-old` gets its Windows dependencies in two ways. It builds SFML 2.5.0 from a
git submodule. It links everything else as prebuilt **x86-only** binaries:

- FMOD Ex 4.44.14, with its Event System at 4.44.20;
- FreeType;
- GLEW;
- and, at run time, zlib.

FrontierOutpost builds x64 and ARM64. The baseline builds (MIGRATION_NOTES.md §9) measured four
facts:

- **The x64 link fails on exactly two dependencies:** 56 GLEW symbols and 28 FMOD Ex symbols
  (runs 2 and 3, `LNK1120: 84 unresolved externals`). Every translation unit compiles.
- **The Win32 original renders its text with FreeType 2.3.5.** `lt.dll` imports its FreeType
  functions from `freetype6.dll`, whose version resource says 2.3.5 (dumpbin `/imports`, run 3).
  The headers it compiles against are 2.5.3, and it also links a static `freetype28s.lib` and SFML's
  bundled 2.5.5, neither of which contributes at run time.
- **On x64 the same calls bind to SFML's bundled static FreeType 2.5.5**, because the linker
  skips the x86 libraries (LNK4272).
- **FMOD Ex was never built for ARM64.**

## Decision

| Dependency | How it is supplied | x64 | ARM64 |
|---|---|---|---|
| **SFML 2.5.0**, commit `192eb968` | Vendored source in `FrontierOutpost/ext/SFML`: sources, headers, `extlibs/headers`, and the Windows `extlibs/libs-msvc-universal` and `extlibs/bin`. Our own `.vcxproj` per module builds it as the original's CMake did. The bundled prebuilt libraries are kept with the tree but **linked by nothing**: FreeType comes from source, and `sfml-audio` is linked by nothing. | built | built |
| **FreeType 2.5.5**, tag `VER-2-5-5`, commit `232bd948` | Vendored source in `FrontierOutpost/ext/freetype`: `include/`, `src/`, `builds/windows/ftdebug.c`, and the licence texts (`FTL.TXT`, `GPLv2.TXT`, `LICENSE.TXT`). Our own static-library `.vcxproj` is modelled on FreeType's `builds/windows/vc2010` project: the same 39 sources and definitions. `lt` links it for both `liblt` and `sfml-graphics`. It replaces the original's `freetype.lib` / `freetype6.dll` (2.3.5), `freetype28s.lib`, and so also `zlib1.dll`. | built | built |
| **GLEW 1.7.0** | `src/glew.c` and `LICENSE.txt` from the official release `glew-1.7.0.tgz` (SourceForge; SHA-256 `1653a63fb1e1a518c4b5ccbaf1a617f1a0b4c1c29d39ae4e2583844d98365c09`). Its headers are the vendored `include/Glew/GL`, whose `glew.h`, `wglew.h` and `glxew.h` are byte-identical to the release's apart from line endings. Built as the static `glew32s` with `GLEW_STATIC` and GLEW's own static-build definitions, replacing the original's x86 `glew32s.lib`. | built | built |
| ~~**FMOD Ex 4.44**~~ **Superseded by ADR-003: XAudio2 replaces it, and every FMOD file is deleted.** | Prebuilt and proprietary; there is no other way. **x64 supplied by the owner** into `extlib/x64/FMOD` and `extbin/x64`. The original's x86 files are not carried over (MIGRATION_NOTES.md D14, amending this ADR in Phase 1): no FrontierOutpost platform can use them. The tree carries no FMOD licence text, so the owner's supply should include FMOD's terms. | owner-supplied | **none: documented blocker** (MIGRATION_NOTES.md D9) |
| OpenGL, GLU, Windows SDK import libraries | The Windows SDK (10.0.26100.0 on the build host) | system | system |
| Runtime assets (`resource/`) | The LFS pointer files, as `ltheory-old-main/` has them (D12) | — | — |

**No package manager.** vcpkg and NuGet would move FreeType to 2.13 and GLEW to 2.2, away from
headers the code includes by path. They would also add a tool the build depends on.

**FreeType is a BEHAVIOUR-RISK** against the Win32 original. FreeType 2.4 made the TrueType
bytecode interpreter the default hinter, so glyphs may render differently from 2.3.5. 2.5.5 was
chosen for four reasons:

- the original's own x64 build resolves to it;
- it is exactly SFML's bundled version (the SFML headers are byte-identical to the tag's);
- it is ABI-compatible with the 2.5.3 headers `liblt` compiles against. With comments and
  whitespace stripped, `FT_Bitmap`'s fields differ only in signedness, and none of the eleven
  functions `LTE/Font.cpp` calls changes signature;
- it avoids 2.3.5's known font-parsing defects.

## What this forecloses

- A package-manager manifest for these dependencies.
- Changing FreeType, GLEW or SFML versions as part of the migration.
- ARM64 audio through FMOD Ex, and with it an ARM64 link of `lt` and `launch`. That holds until
  the owner chooses one of MIGRATION_NOTES.md §8.2's options (ARM64EC, an FMOD 2.x port, or the
  original's null engine).
