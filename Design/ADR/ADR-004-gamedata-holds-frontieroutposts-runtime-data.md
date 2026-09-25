# ADR-004: GameData holds FrontierOutpost's runtime data

- **Status:** Accepted (owner, 2026-09-25, after the migration's Phase 5)
- **Scope:** `GameData/`, and how `launch.exe` finds it
- **Detail:** `FrontierOutpost/MIGRATION_NOTES.md` D26–D28, §22, O13 and O14. This amends
  ADR-001's scope and supersedes ADR-002's row for the runtime assets.

## Context

The original keeps everything its engine loads at run time in one tree, `resource/`: fonts,
sounds, textures, game data, LTSL scripts and shaders, 630 files in the migrated copy. The engine
reads the tree through a single root, `kResourcePath` in `src/liblt/LTE/Location.cpp`, relative
to the working directory. The font loader names its own folder, `resource/font/`
(`src/liblt/LTE/Font.cpp`). `launch.exe` looked for `resource/` in its working directory and, when
it was not there, changed to the parent folder once.

Phase 1 copied the tree to `FrontierOutpost/resource/`. Its 230 Git LFS files arrived as pointer
files (MIGRATION_NOTES.md D12), because this repository does not use Git LFS and the session doing
the migration cannot reach the original's LFS storage. FrontierOutpost builds into
`bin\<Platform>\<Configuration>\`, three folders deep, so `launch.exe` had to be started from
`FrontierOutpost/` (§10.2).

What the program writes goes to `./cache/`, and it reads mods from `./mod/`. Both are relative to
the working directory (§5.8). AGENTS.md R13 asks for every runtime file to be named with its form
and lifetime, and for written paths to resolve against a known location.

## Decision

1. **The runtime data live in `GameData/`, at the repository root**, beside `FrontierOutpost.slnx`,
   `FrontierOutpost/` and `ltheory-old-main/`. All of the migrated `resource/` moved there, with
   its layout and file names unchanged. The engine's root is `GameData/` (`Location.cpp`), and so
   is the font loader's (`Font.cpp`). `GameData/` is required: without it, `launch.exe` finds no
   script to run.
2. **`launch.exe` works from the folder that holds `GameData/`.** At start, it looks in its own
   folder and then in each parent folder in turn. It changes its working directory to the first
   one that has `GameData/`. If none has, it stays in the working directory it was started in. So
   it runs from anywhere, including Visual Studio's debugger, and the engine's paths stay relative
   as the original wrote them.
3. **What the program keeps beside `GameData/` is the original's, and is not versioned:**
   - `cache/` is created at first use. It holds `config.txt`, `settings.bin`, the logs
     `logErrors.txt` and `logAsserts.txt`, `crashdumps/`, `screenshot/`, and cached script and
     function results under `cache/cache/`.
   - `mod/` is read if it is there, and never created.

   Both live until someone deletes them, and both are git-ignored. Because of decision 2, they
   land in the one known folder, beside `GameData/`.
4. **The real files are ordinary Git objects, not Git LFS.** A one-off job of the migration
   workflow fetched them from the original at `0535d46`. It checked each file against its
   pointer's size and SHA-256, and committed them (§22).
   - Every font lands with its licence text beside it (AGENTS.md R14). The original ships the
     three Noto fonts without theirs, and their own metadata names the Apache License 2.0, so its
     text was added.
   - **The 79 Ogg sounds stay pointer files** until the owner decides how they land. D16 has the
     owner converting them to WAV (O13).
5. **`GameData/` is part of the legacy import.** ADR-001's exemption covers it as it covers
   `FrontierOutpost/`:
   - its names, layout and formatting stay the original's;
   - AGENTS.md §1, §2 and §4 do not apply to it;
   - no checker gates it.

   `GameData/.gitattributes` has Git store its media byte for byte.

## What this forecloses

- **Git LFS for these assets.** Every clone carries them in its history for good: 151 files and
  214.1 MB now, and more once the Ogg sounds land.
- **Paths that depend on where `launch.exe` was started**, whenever a `GameData/` exists above it.
- **A packaged layout that puts `GameData/` anywhere other than beside `launch.exe` or above it.**
  The original's archive mode (`resources.bin` beside the executable, under `BUILD_RELEASE`) is
  unchanged and still switched off.
- **Renaming or restructuring `GameData/` as part of the migration.**
