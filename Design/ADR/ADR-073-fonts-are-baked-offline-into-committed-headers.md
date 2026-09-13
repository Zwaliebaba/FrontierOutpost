# ADR-073 — Fonts are baked offline from TTF into committed headers, and MSBuild never learns about it

**Status:** Accepted 2026-09-13 (owner)

**Date:** 2026-09-13
**Decided by:** Design session on the font question, at the owner's request. The owner set three constraints in order: the executable keeps shipping alone; the bake does **not** run under MSBuild; and the faces are IBM Plex Mono and IBM Plex Sans, which fixes the source format as TTF.
**Supersedes:** —

---

## Context

The game has one font and has had one since the beginning: 96 glyphs of printable ASCII, 8×8, one
bit a pixel, 768 bytes hand-typed as a `constexpr` array in `NeuronClient/Font.h`. `FontRenderer`
unpacks it once into a 768×8 `R8_UINT` atlas, one texel per glyph pixel, and draws strings as quads
that read it with `Load()`. No sampler, and a glyph pixel is lit or discarded with nothing in
between (ADR-011, ADR-014).

That font is why five characters in the reference copy are substituted in `MatchFixture.cpp` — `·`,
`−` and `–` become `-`, `→` and `›` become `>` — and why six strings are shortened to fit at 8px.
ADR-014 records both.

Three assumptions are load-bearing. **Fixed pitch:** `MeasurePixels()` is `size() * 8 * scale`.
**One bit a pixel.** **Ninety-six ASCII glyphs**, indexed `code - 32`.

Measured 2026-09-13 by grep: 73 lines outside `FontRenderer` name a font metric, across six files,
but nearly all call `MeasurePixels` — which becomes a sum under a proportional face and needs no
edit at the call site. The genuine fixed-pitch leak is **seven** `FitCharacters` calls and **eleven**
character-counting `Wrap` calls in product code, plus eight assertions in `NeuronClientTests`.
Separately, **81** `DrawText` calls across `SeatsPage.cpp`, `ConnectionDialog.cpp`,
`DesignTokens.cpp`, `JoinPage.cpp`, `MainPage.cpp` and `MapRender.cpp` will each have to name a
face once there is more than one.

What does **not** bind is size. Measured 2026-09-13: `x64/Release/Lockstep.exe` is 867,840 bytes and
`Font.h` is 768 of them. Five anti-aliased faces at a ~13px cell, subset to the glyphs this game
draws, are on the order of a hundred kilobytes. That is a hundredfold increase on `Font.h` and
still noise against the binary.

Nor does R13 bind the way it first reads. R13 forbids a **runtime file dependency** — an assets
folder, a data directory, a working-directory assumption. It says nothing about how the bytes in
the header got there. The tree already contains the stronger precedent: shaders are authored as
`.hlsl`, compiled at build time, and reach the binary as generated headers. R14's ban on
third-party dependencies binds what the executable links, not what a tool on the author's machine
does beforehand — and the offline tooling this needs (`fontTools`, `freetype-py` or Pillow,
none of them present on this machine as of 2026-09-13) never enters the tree.

Finally: `Design/blueprint.md` §7 and `Design/Reference/mobile-portability.md` §1 both record that
the phone client is a **new client rather than a port**. Nothing decided here constrains mobile.

## Options considered

### A. Keep `Font.h` hand-maintained

Add glyphs as more hex in the same array. It costs nothing structurally and is the right answer if
what is wanted is the five missing characters this week.

Rejected as the destination. Hand-authored hex does not scale past the glyph count it already has
and cannot be reviewed — a diff changing `0x6C, 0x6C, 0x7F` to `0x6C, 0x7F, 0x6C` is unreadable as
the thing it actually is, which is a letter changing shape. Five faces of a real typeface is not
reachable this way at all.

### B. Embed the TTFs and rasterize at runtime

`stb_truetype` or FreeType compiled into the client, the font bytes as `constexpr` arrays. Still one
file, and it buys glyphs at arbitrary sizes.

Rejected. It breaks R14 for a benefit the desktop client cannot use — the screen is fixed at
1280×720, so there is no density to scale to — and it moves a rasterizer, its memory and its
failure modes into the shipped binary to compute at every startup something that is identical on
every machine.

### C. Bake under MSBuild, as the shaders are baked

A custom build step alongside `FXCompile`: TTF in, generated header out, `.gitignore`'d, regenerated
every build.

Rejected by owner decision on 2026-09-13. The shader pipeline earns its integration because `.hlsl`
files change often; a font changes a handful of times in the life of a project. Against that, the
integration costs a rule in two `.vcxproj` files and their `.filters`, a `SKIP_DIRECTORIES` entry
and a registration check in `Build/CheckProjectFiles.py`, a `.gitignore` entry, and a build that
now fails on any machine missing a Python font library. A standing tax on every build, for a file
that changes twice a year.

### D. Bake offline, commit the header

A tool the author runs by hand turns checked-in TTFs into generated headers, and those headers are
committed exactly as `Font.h` is committed today. MSBuild learns nothing.

Its one real cost is drift: nothing forces the committed header to match its source. Because the
rasterizer cannot be a CI dependency under this option, the gate is a **hash** rather than a
re-bake — see the Decision.

## Decision

**Fonts are baked offline from TTF into committed headers.** Option D.

`Build/Fonts/*.ttf` are the sources, checked in. `py Build/BakeFont.py` is the bake; it runs on the
author's machine, never under MSBuild, and writes `NeuronClient/Font.h`. The header keeps its name
and its sentence in R13 and simply stops being typed by a human. `Build/Fonts/` sits under `Build/`
rather than beside the C++ because AGENTS.md §2 gives a project directory exactly two sanctioned
subdirectories, and this decision declines to spend the third on files MSBuild never reads.

The baker may use whatever it likes on the author's machine — `fontTools` and `freetype-py`, or
Pillow — because R14 binds the tree and not the toolbox. Its **output** is plain `constexpr` arrays
and nothing else.

**The freshness gate is a hash, not a re-bake.** `Build/BakeFont.py` writes into the generated
header the SHA-256 of every source TTF, of the baker itself, and of the generated content.
`Build/CheckProjectFiles.py` verifies all three with the standard library alone — so CI needs no
rasterizer, and the check still catches a swapped font, an edited baker and a hand-edited header.
It cannot catch a non-deterministic rasterizer, which is why the baker pins its parameters in the
header's provenance comment.

**One header, several faces.** `Font.h` holds every baked face behind a `Face` enumerator rather
than one file per face, because the atlas is one texture and the faces are chosen per draw call.
Which faces, at what size, and in what weights is **ADR-074**, not this ADR: this one decides the
pipeline, and the pipeline is face-agnostic.

**What this ADR does not decide.** Whether glyph coverage becomes alpha — ADR-014's rule that alpha
is a material and not coverage — is ADR-074's to answer. The two arrive together in practice and
are separable in principle, and separating them is what lets the pipeline be built and proved
before the screen changes.

## Consequences

**What this makes easy.** A face is a TTF and a line in the baker. The five characters ADR-014 had
to substitute stop being a font problem. Localization stops being architecturally blocked: a
codepoint index is what `spaceoutpost` was missing when it answered the same question with one
atlas per language.

**What this makes hard.** The derived header is committed, so it can go stale, and the hash gate is
what stands between that and a defect. Re-baking requires the offline toolchain — not present on
this machine as of 2026-09-13, so the first stage of the plan is installing it. A developer without
it can build and run the game but cannot change a font, which is the correct trade for a file that
changes twice a year and the wrong one for anything that changes weekly.

**What it costs.** `Font.h` goes from 768 bytes to roughly a hundred kilobytes, and from reviewable
to opaque — nobody will read that diff, which is exactly why the hash gate exists. Eighteen call
sites and eight assertions change before anything is visible.

**What it forecloses.** Nothing on mobile, which is a new client. Not a return to a hand-authored
face either: `Font.h` keeps its shape, so reverting is `git revert`.

## What this changes elsewhere

- **Code:** `Build/BakeFont.py` and `Build/Fonts/*.ttf` are new. `NeuronClient/Font.h` becomes
  generated output, committed. `Build/CheckProjectFiles.py` gains the hash gate. No `.vcxproj`, no
  `.filters`, no `.gitignore` changes.
- **AGENTS.md:** R13 gains a sentence. Drafted here and applied in the commit that makes it TRUE.
  `Font.h` is hand-typed until FONT-01 stage 1, and a rule describing a tree that does not exist yet
  is the failure `Design/README.md` §3.1 is written against:
  > *`NeuronClient/Font.h` is generated. `Build/Fonts/*.ttf` are the sources and
  > `py Build/BakeFont.py` is the bake; it runs on the author's machine, never under MSBuild, and
  > the header is committed. `Build/CheckProjectFiles.py` fails when the recorded hashes disagree.
  > Editing the header by hand is a defect.*
- **Design/:** `Design/Plans/FONT-01-PlexFaces.md` is the staged work. ADR-074 decides the faces.

## Open questions

**Font licensing has nowhere to live.** Baking glyphs into the binary is distributing the font. IBM
Plex is OFL-1.1, which permits it and asks for the licence to travel with the font — and R13 says
nothing ships beside `Lockstep.exe`. The notice therefore has to go *inside* the binary: a comment
in the generated header is not distribution, so it needs a Win32 version-resource string or a line
on an about screen. **Owner decision, not taken.**

**Whether the hash gate belongs in `CheckProjectFiles.py` or a fourth checker.** It is a build-shape
check and that file already owns those; it is also already the longest of the three.

**What the codepoint subset should cover.** ASCII plus the five substituted characters is the
minimum. Wider coverage costs only atlas area and bake time, and no localization is on any roadmap —
so covering it now would be building for a requirement nobody has stated.
