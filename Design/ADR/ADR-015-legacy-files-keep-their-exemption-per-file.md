# ADR-015: Legacy files keep their exemption, file by file

- **Status:** Accepted (owner, 2026-09-26)
- **Scope:** the ltheory-old import after ADR-014 spread it over NeuronCore, NeuronClient,
  GameLogic and FrontierOutpost. It narrows ADR-001, whose exemption was a folder
  (`FrontierOutpost/`), to a marked list of files.

## Context

ADR-001 exempted everything under `FrontierOutpost/` from AGENTS.md §1, §2, §4 and parts of §3,
because the import cannot conform without being rewritten. ADR-014 moves that code into four
projects that also hold, or will hold, new code, which AGENTS.md governs in full. A folder no longer
tells the two apart.

## Decision

1. **A legacy file is marked in its project:** `<Legacy>true</Legacy>` on its `ClCompile` or
   `ClInclude` item. The project file has to list the file anyway, so it is the one place the list
   is kept.
2. **Every project states AGENTS.md's settings** (`/W4 /WX /fp:precise`, Unicode), which is what a
   new file gets. `Build/Legacy.targets`, imported by every project, gives each marked source the
   settings of ADR-001:
   - `/W3` without `/WX`, and `/fp:fast`;
   - `/EHs`, and the multi-byte character set (`_MBCS`, `UNICODE` undefined);
   - `WIN32` and `_WINDOWS`, and `/Zc:__cplusplus`;
   - no precompiled header, and no `/utf-8`.
3. **The checkers read the same marker.** `Build/ProjectModel.py`'s `IsExempt` leaves a marked file
   out of R2, R7, R11, clang-format and clang-tidy. `RunClangTidy.py` also excludes marked headers
   by name, so a first-party file that includes one is not reported for it. Registration and layout
   (§2) apply to marked files as to any other.
4. **What a legacy file loses from ADR-001:** Release now has debug information and `/Gy /Oi` (the
   projects' settings). The original's per-configuration differences were about optimisation and
   are otherwise unchanged. The build's warnings were compared before and after the split: the same
   319 unique warnings in Debug|x64, none new and none lost.
5. **A file loses its marker when it is brought up to AGENTS.md,** and from then on it is checked like
   any other.

## What this forecloses

- Exempting a folder, or a whole project, again.
- New code with the marker. A new file follows AGENTS.md, even when it is carved out of a legacy
  one, unless it is a verbatim move of legacy code, as the untangling in
  `Design/LibrarySplit-plan.md` P2 does.
