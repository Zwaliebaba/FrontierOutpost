# ADR-035 — The game is LockStep: Universe

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Owner decision, 2026-09-11
**Supersedes:** —

---

## Context

The game was called *Frontier Outpost*. That name was recorded as settled rather than provisional in
`Design/blueprint.md` §Status on 2026-09-11 ("**Frontier Outpost is the name**, not a working
title"), and the owner changed it to **LockStep: Universe** later the same day, with **Lockstep** as
the short form for running prose and for every identifier.

The old name was not confined to prose. It was the `Frontier` namespace that AGENTS.md R9 names
explicitly, the `FrontierOutpost/` directory, `FrontierOutpost.slnx`, `FrontierOutpost.exe`, the
`RootNamespace` of the application project, the window class and title, the three message-box
captions, the top bar's brand text, the match store and log filenames, and thirty-three documents.
A rename that reached only some of those would leave the tree speaking two vocabularies — the defect
AGENTS.md R11 names for spellings: a reader has to know which half they are in, and a grep for one
name finds half the uses.

Four constraints bound the change, and each of them changed what got done:

**"Frontier" is also galaxy vocabulary.** `SystemKind::Frontier`, `frontierSystemsPerPlayer`,
`frontierLaneMinimumTicks`, `frontierLaneMaximumTicks`, `GalaxyRejection::FrontierLaneOutOfBand`,
`FRONTIER_RING_RADIUS_X/Y` and the priority queue in `Galaxy.cpp` all mean the contested middle of
the galaxy, or the working set of a shortest-path search — not the product. A word-boundary
substitution on `Frontier` would have silently renamed the galaxy generator's own concepts. The
rename was therefore anchored on `Frontier::` and `namespace Frontier` only, and every bare
`frontier` was left alone.

**The top bar has no width to give.** `MainPage::DrawTopBar` carries a note that at 8px the
reference's spelled-out bar is already 63px wider than the frame (ADR-014), which is why the match
line is abbreviated to `M0419 · D12/21`. `LOCKSTEP: UNIVERSE` is two glyphs wider than
`FRONTIER OUTPOST`; `LOCKSTEP` is eight narrower.

**An Accepted ADR is immutable except for its status line** (`Design/README.md` §4). Nineteen ADRs
name the old namespace, the old directory or the old solution.

**The name collides with this game's own mechanism.** Measured across tracked text files on
2026-09-11 by word-bounded case-insensitive match: 312 occurrences of `lock`/`locks`/`locked`/
`locking` in the mechanism sense, against 1,798 occurrences of the product name. ADR-026 is titled
*the schedule is arithmetic and owes every lock*. ADR-018 §C rejects an alternative named *Lockstep
with checksums*. The one-pager's hook sentence is "Your orders lock at the next tick, theirs do too."

## Options considered

### A. Keep *Frontier Outpost*

Costs nothing and keeps the record's vocabulary unambiguous: the product is Frontier Outpost, a lock
is a lock, and the synchronisation model has a name nobody confuses with the game. It also keeps a
name the owner no longer wants, which is not a small thing for a project whose whole purpose is that
someone enjoys building it. Rejected on that ground alone; the argument against it is not technical.

### B. Rename the documents, leave the code

Cheapest change that satisfies the literal request ("update all documents"): thirty-three Markdown
files, no build risk, nothing to compile. It was rejected because it manufactures exactly the split
vocabulary the design record is supposed to prevent — every ADR would say *Lockstep* while every
header said `namespace Frontier`, and every path citation in the record would point at a directory
whose name the documents no longer use. A reader would have to hold both names to navigate the tree.

### C. Rename everything: prose, namespace, and build identity

The whole surface in one commit: `Frontier` → `Lockstep` (1,589 qualified call sites, 34 namespace
declarations), `FrontierOutpost/` → `Lockstep/`, `FrontierOutpost.slnx` → `Lockstep.slnx`, the
executable, the window class and title, the dialog captions, the top bar, the store and log
filenames, and every document. Largest diff and the only option that leaves one vocabulary behind.
The cost is that it cannot be verified here: the tree is MSVC, D3D12 and Win32, so the first real
proof that it builds is CI on Windows.

## Decision

The game is **LockStep: Universe**, shortened to **Lockstep** wherever a short form reads better,
and spelled `Lockstep` in every identifier — namespace, directory, project, solution and executable.
Option C: the rename reaches prose, code and build identity in one commit. Full-title spellings are
`LockStep: Universe`; the single-token identifier form is `Lockstep`, which matches the codebase's
existing one-word `PascalCase` namespaces (`Neuron`) rather than introducing an internal capital.

The on-screen brand in the top bar is `LOCKSTEP`, not the full title, because the bar is already
over-width (Context, above). The OS window title and the three message-box captions carry
`LockStep: Universe`, where there is no width constraint and the full name is what a player should
see.

Nineteen Accepted ADRs were edited, against the immutability rule, and only where the old name was a
**pointer into the tree** — `Frontier::Match`, `FrontierOutpost/MainPage.cpp`,
`FrontierOutpost.slnx` — with two prose exceptions: ADR-014's Context now calls the screen "the
single screen of Lockstep", and ADR-030's description of a past `MatchLog` bug keeps the historical
`frontier-match.log` and gains a clause naming the file's current spelling, because renaming it
outright would have the ADR report a bug under a filename that did not exist when it happened. No
decision text, no rationale, no rejected option and no status line was changed in any of them.

The reasoning is that the immutability rule exists so the record shows what was *believed* at the
time, and a rename changes the referent rather than the belief; a citation
left pointing at a namespace that no longer exists is a defect under `Design/README.md` §3.1, not
fidelity to history. `Design/README.md` §2 records this exception, as it already records the
unrenamed `Design/Screens/` citations.

## Consequences

**The product name and the mechanism now share a word, and the record has to live with it.** This is
the real cost and it is not recovered by anything else in this ADR. "The lockstep lock", "Lockstep
locks orders at the tick" and "lockstep determinism" are now sentences in which a reader must infer
from context whether the subject is the game or the synchronisation model. The mitigation is
convention, not tooling: **write the product name as *Lockstep* or *LockStep: Universe* and never as
a bare lower-case `lockstep`**, and keep `lock` for the order-lock mechanism. ADR-018 §C is the one
place the old lower-case usage survives, inside a rejected option's title, and is left as written.

**The `Lockstep` namespace sits on the game side of the engine/game seam, describing an engine
concern.** R9 splits the tree so that `Neuron` knows nothing about this game and `Frontier` was
game vocabulary by construction (`MatchState.h` says so in a comment). `Lockstep` is a
synchronisation term, so the seam's naming no longer explains itself: a reader could reasonably
expect `Lockstep` to hold the tick schedule and the frame stream, which are `Neuron`'s. R9 and that
comment were updated to say `Lockstep`; the justification they carried is weaker than it was.

**The first proof that this builds is CI.** Nothing in the tree compiles off Windows.
`Build/CheckProjectFiles.py` and `Build/CheckFormat.py` were run and are clean — the checker reports
the same eight pre-existing `Shaders\`-versus-`Shaders/` separator complaints before and after the
rename, which are an artefact of running a Windows script on Linux and name nothing that was
renamed. `Build/RunClangTidy.py` needs the MSVC include paths and could not run.

**An existing match store and log will not be found.** The hosted server writes
`lockstep-match.store` and `lockstep-match.log` beside the executable; anything written under the
old `frontier-` prefix is orphaned rather than migrated. This is a prototype that has never been
played by six people on six machines (blueprint §Status), so there is no store worth migrating, and
a migration path is not worth the code.

**A checkout will have a stale output directory.** `x64\Debug\FrontierOutpost.exe` is not cleaned by
the rename, and `Lockstep.exe` lands beside it. Delete `x64\` once.

## What this changes elsewhere

- **AGENTS.md:** R9 now names `namespace Lockstep`. The naming table's namespace example is
  `Neuron`, `Lockstep`, and its macro example is `LOCKSTEP_ASSERT` (illustrative — no such macro
  exists in the tree). Every `FrontierOutpost.slnx` build invocation, the project table and the
  dependency diagram are renamed.
- **Design/:** `blueprint.md` §Status and the §9 decision table record that the name was settled and
  then changed on the same day, and cite this ADR. `README.md` §2 records the ADR exception above.
  `space-4x-one-pager-v10.md`, `UI/README.md` and `UI/DESIGN-GUIDELINES.md` are retitled.
  `UI/SCREENS.md`'s top bar reads `LOCKSTEP`.
- **Code:** `namespace Frontier` → `namespace Lockstep` across `GameLogic`, `Lockstep/` and the four
  test suites; `FrontierOutpost/` → `Lockstep/`; `FrontierOutpost.slnx` → `Lockstep.slnx`;
  `FrontierOutpost.sln.DotSettings` → `Lockstep.sln.DotSettings`; `FrontierOutpost.cpp/.h` →
  `Lockstep.cpp/.h`; `RootNamespace`, window class, window title, dialog captions, top bar brand and
  the store/log prefixes. Done in this commit. Not compiled — see Consequences.
- **Build/CI:** `CheckProjectFiles.py` and `RunClangTidy.py` project maps, `CheckFormat.py`'s
  generated-file list, `.clang-tidy`'s `HeaderFilterRegex`, `Screenshot.ps1`'s example path, the
  workflow's `msbuild` line and the PR template's build checkbox.

## Open questions

**The repository is still named `FrontierOutpost`.** Renaming it on GitHub is outside a commit's
reach and would move the clone URL; it is left to the owner.

**The design canvas `Frontier Outpost Main Page.dc.html` is not renamed.** It lives outside this
repository, so `Design/UI/README.md` keeps the old filename with a note saying why. If that file is
ever renamed, the citation goes with it.

**Whether `Lockstep` is the right namespace for the game side of the seam is not settled here.** The
second consequence above is a real weakening of R9's rationale, and a future session that finds
itself explaining which `Lockstep` a reader is looking at should write the ADR that splits the
product name from the namespace rather than re-deriving the argument.
