# ADR-110 — The executable is a Release asset, on two channels

**Status:** Accepted

**Date:** 2026-09-15
**Decided by:** Owner decision: both channels, and the bare `Lockstep.exe` rather than an archive.
**Supersedes:** — (amends the **Distribution** row of `Design/README.md` §1)

---

## Context

`Design/README.md` §1 has said since the first week what the deliverable is: **one executable,
shipping alone**. Art, colour tables, fonts and compiled shaders are inside the binary — the shaders
through `FXCompile` into `CompiledShaders\*.h`, the font baked offline into a committed `Font.h`
(ADR-073) — so there is no assets folder to zip beside it and nothing to install. The whole delivery
is a single file of a few hundred kilobytes.

**Nothing produced one.** CI built `Debug|x64`, ran the suites, and threw the binary away with the
runner. Someone who wanted to play had to clone the tree, install Visual Studio 2026 with the v145
toolset, and build it. For a game whose front page shows screenshots, that is the entire audience
gone.

Two facts bound what could be done about it.

**A Debug build is not a thing that can be handed to anybody.** It links the debug CRT —
`ucrtbased.dll`, `vcruntime140d.dll`, `msvcp140d.dll` — which the Visual Studio licence does not
permit redistributing and which is present only on a machine that has Visual Studio on it. On any
other machine it fails in the loader, before `WinMain`, with a dialog naming a DLL. So publishing
the binary CI already had was never an option: the decision to publish is necessarily also a
decision to build `Release|x64`, which AGENTS.md §6 had deliberately declined to do for anything but
`GameLogicTests`.

**A workflow artifact is not a download.** `actions/upload-artifact` — which this workflow already
uses for `build.log` and `TestResults` — attaches a file to a *workflow run*. It never appears on
the repository's front page, it is re-zipped whatever it holds, it expires after 90 days, and on a
public repository as much as a private one it cannot be fetched without a GitHub account. It is the
right home for a build log, whose reader is whoever is debugging the run. It is the wrong home for a
game, whose reader has never seen the repository.

## Options considered

### A. Leave it, and let people build from source

Costs nothing and keeps CI at three jobs. It is also the status quo whose consequence is stated
above: the audience is people who own Visual Studio 2026 and will spend twenty minutes on a hobby
game. There is no version of this project where that is the intended reach.

### B. Upload the executable as a workflow artifact

One line in the existing Debug job, near-zero cost, and it does solve the *developer's* problem:
grab the binary a PR produced without building it. It does not solve the one asked about, for the
two reasons above — login required, and a Debug binary will not start. Making it a Release artifact
fixes half of that and leaves the login.

### C. A versioned Release on a `v*` tag, only

The classic shape. A tag push builds `Release|x64`, the executable is attached to a Release, the
sidebar shows it, the URL is permanent and anonymous. Releases are immutable, which is exactly right
for a version.

What it costs is that nothing exists between tags. On a tree committing several times a day with no
version scheme yet, the honest answer to "where do I get the current build" stays "you don't".

### D. A rolling `latest-build` prerelease on every green `main`, only

Always current, no tagging ceremony, one stable URL that always serves today's game. The cost is
that the download has no identity: two people reporting the same crash may be running different
binaries, and there is no way to ask which. It is also the only channel, so a version, when there is
one, has nowhere to live.

### E. Both

C and D from one workflow. `latest-build` is marked **prerelease**, which is what stops the two
colliding: GitHub's "Latest" badge and the `/releases/latest` redirect both skip prereleases, so the
day a real `v*` exists it becomes what the front page offers, while the rolling build stays
reachable by its own permanent URL for anyone who wants the tip.

The cost is a Release build of the application on every push to `main`, and two release channels to
explain instead of one.

## Decision

**E.** `Lockstep.exe` is built at `Release|x64` and published as a GitHub Release asset on two
channels: a `v*` tag produces a versioned Release, and a push to `main` deletes and recreates the
`latest-build` prerelease at the new commit. The asset is the **bare executable**, not an archive —
the game is one file, and asking somebody to unzip a single file to run it is ceremony for nothing.

The work is two jobs in the existing workflow. `package` builds `Lockstep` at Release and hands the
executable on; it runs in parallel with the two gate jobs so it adds nothing to the wall clock, and
is skipped on pull requests. `publish` runs on Linux, does nothing but call `gh`, and `needs` all
three gate jobs — **a red tree cannot produce a public download**.

The rolling release is deleted and recreated rather than edited, because the tag has to move to the
commit that was built and neither `gh` nor the API will move a tag under a release that already
exists. The job verifies afterwards that the tag landed on the built commit, because a
`--cleanup-tag` that silently left the tag behind would serve today's executable under a tag whose
source zip is somebody else's commit.

## Consequences

**Every shipping project is now compiled both ways on `main`.** This was a side effect and it is
worth more than the download. `Lockstep`, `NeuronClient`, `NeuronServer` and `LockstepClient` had
never been built at Release by anything; the determinism job reaches only `GameLogic` and
`NeuronCore`. A break that needs `/O2`, `NDEBUG` or LTCG to show itself used to surface on the day
somebody shipped. It now surfaces on the push. The four test projects other than `GameLogicTests`
are still Debug-only, and a pull request still builds no Release beyond the determinism gate.

**What is published has never been run.** GitHub's runners have no GPU, so the binary on the
Releases page has been compiled, linted and had its simulation hashed, and has not drawn a frame.
AGENTS.md §3 already says a green build says nothing about whether the game draws; this makes that
gap public. The release notes say so in as many words.

**The binary is unsigned, and SmartScreen will interrupt the first run.** A code-signing certificate
is money and an identity check, and neither is justified yet. The notes tell people what they will
see and what to click, which is the honest version of a problem that is not being solved.

**It needs the Visual C++ Redistributable.** `RuntimeLibrary` is unset in every `.vcxproj`, so
Release inherits `MultiThreadedDLL` and the executable links the CRT dynamically. Switching Release
to `/MT` would make it genuinely standalone and is *rejected*: it would be a setting on which Debug
and Release disagree that is not about optimisation, which `CheckProjectFiles.py` exists to forbid
and AGENTS.md §3 states as a rule. The redistributable is a link in the notes instead.

**The executable carries no version.** `Lockstep.rc` holds an icon and nothing else — no
`VERSIONINFO` — so two copies of `Lockstep.exe` on a disk are indistinguishable, and a copy from
`latest-build` cannot be told from a copy of `v0.1.0` without hashing it. The tag is the only
version there is. This is the sharpest cost of shipping a bare file under a fixed name and it is not
addressed here.

**`main` becomes a publishing branch.** Every green push rewrites a public tag and replaces a public
binary. On a repository with one developer and no watchers that is free; it would not be on a busier
one.

## What this changes elsewhere

- **AGENTS.md:** §6's CI table gains the two jobs and loses "the only thing built twice"; §6's
  Release paragraph and §3's "Release is compiled by whoever ships" are both amended, because they
  are no longer true of `main`. Done in this commit.
- **Design/:** the **Distribution** row of `README.md` §1 now says where the executable goes, not
  only what is in it. Done in this commit.
- **Code:** nothing. No `.vcxproj`, no source file and no build script is touched — this is entirely
  `.github/workflows/build.yml`.
- **Tests:** nothing. The jobs are exercised by running them.

## Open questions

**Whether `Lockstep.rc` should carry a `VERSIONINFO`.** The consequence above is real and the fix is
small — a `VERSIONINFO` block stamped from the tag, or a constant bumped by hand — but a version
number in the binary is a decision about how this project versions itself, which has not been taken.
Until it is, the download's identity is its URL.

**Whether a pull request should be able to produce a build somebody can try.** A Release artifact on
the PR, downloadable by anyone logged in, would let a reviewer run a change instead of reading it.
It is option B, which was rejected as an answer to *this* question, and is a reasonable answer to
that one.

**Whether the rolling channel survives the first version.** `latest-build` earns its place while
there is no `v*` at all. Once versions exist, "the tip of `main`" may be a thing only the author
wants, and one fewer channel is one fewer thing to explain.

**Code signing.** Not now, for the reasons above. The trigger to revisit it is somebody reporting
that SmartScreen stopped them, rather than warned them.
