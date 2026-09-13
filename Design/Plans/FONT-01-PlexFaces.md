# FONT-01 — From one hand-typed 8×8 font to five baked cuts of IBM Plex

**Status:** **In flight. Stages 0 to 5 done 2026-09-13; stage 6 is next.** Written 2026-09-13 against ADR-073 and ADR-074,
both Accepted by the owner the same day.

**Stage 0 verification, as run (2026-09-13).** The three checkers pass, Debug|x64 builds, and all
five suites pass — 507 tests. The join screen was captured from a build of each side and the two
PNGs are byte-identical (same length, same SHA-256), which covers `JoinPage.cpp`'s converted site.
**The main page was NOT captured byte-identically, and the reason is worth writing down**: the
client reaches that screen only by joining a `--serve` peer, the match advances on a schedule the
store pins to the tick seconds it was created with, and two captures are therefore never at the same
tick — `--tick 3600` on a resumed store does not freeze it. What was verified there instead is that
the page renders correctly and that the orders rail's help line wraps at exactly the same three
points as the pre-change build. Closing that gap needs a replay-to-tick or frozen-clock mode the
client does not have; it is not a font problem and is not in this plan's scope.

This plan implements ADR-073 (how a font gets into the binary) and ADR-074 (which fonts, and
coverage as alpha). Read both first. This says *in what order*, *how you know each step worked*,
and *where the commits fall*.

**The ordering is the whole point of the document.** Stages 0 to 3 change nothing a player can see,
and each is provable by a byte-for-byte screenshot comparison. The screen does not move until
Stage 4, by which time the pipeline, the renderer and the shader have all been proved separately.
Do not reorder this to see Plex sooner; the identical-screenshot test exists only while the face is
unchanged, and it is the strongest evidence this plan has.

**Stage 1 verification, as run (2026-09-13).** `--self-test` reproduces all 768 bytes of
FONT_DATA from the legacy bake. The gate was tested in both directions: a clean legacy bake passes,
one flipped atlas byte fails with exactly the hand-edit message, and a Plex bake re-hashes all five
TTFs and passes. `NeuronClient/Font.h` is deliberately UNCHANGED -- the baker is proved but its
output cannot ship until stage 2 teaches the renderer to read it.

**What stage 1 measured, which settles a question ADR-074 left open.** Plex Mono is exactly 0.600em,
so at 12px it advances **7 pixels** -- one narrower than the font it replaces, which puts 36
characters in the digest rail's 254 where 31 fit today. Cap height is 0.698em, so capitals land
within half a pixel of their current height and the screen stays recognisable. A line goes from
12px to 16px, and that is the whole of the cost. Plex Sans at the same size is properly
proportional: `A` is 8px, `i` is 3px, the arrow is 10px.

**Stages 2, 3 and 4, as run (2026-09-13).** All three byte-identical checkpoints came back
byte-identical: the join screen's PNG has the same length and the same SHA-256 across stage 2 (new
header format, `Face`, per-glyph advances, codepoint binary search, UTF-8 decoding, baseline
placement) and across stage 3 (`R8_UNORM` atlas, coverage multiplied into alpha). Stage 4 baked
Plex and the screen changed; 514 tests pass. `--legacy` and `--self-test` were deleted with it.

**A defect stage 2 surfaced, fixed rather than worked around.** `Build/CheckFormat.py` fed each
file to clang-format through stdin with `text=True`, which encodes using the locale code page. 1252
holds `·`, `–` and `›` and does **not** hold `→` or `−` — two of the five characters stage 7 puts
back. On such a file the write raised in the parent, the child never saw EOF, and clang-format
waited on stdin forever: no error, no diff, no exit, and through a pipe it read as a hang rather
than an encoding fault. Both subprocess calls now name `encoding="utf-8"` and stdout is
reconfigured, so a diff containing one of those characters prints instead of killing the checker at
the moment it has something to say.

**What stage 4 deliberately left undone.** The layout is not re-derived: a line box is 17px where
the old font's was 8, so labels sit closer to their fields than the design intends and the
vertical rhythm is visibly off on the join screen. That, the gamma on a dark background, and
whether three mono weights are distinguishable are all stage 6, and all three are to be settled
from a screenshot rather than from arithmetic.

**Stage 5, as run (2026-09-13).** Fourteen draw sites are in Plex Sans and the rest stayed mono;
517 tests pass, three of them new. It went in as two commits rather than one, and the first is
worth knowing about before reading the second.

**The face now comes BEFORE the scale in every signature that takes both**, which is the reverse
of the order stage 2 left. The plan flagged the question and left it open; the deciding number is
that **five** call sites in the whole client name a scale, so the old order made the other eighty
write `DEFAULT_SCALE` purely to reach past a parameter they did not care about. The reorder was
safe to make mechanically, because `Face` is an enum class and a scale is a `std::uint32_t`: a
site that was missed is a compile error, not a silently transposed pair. Five were missed, and
the compiler named all five.

**`Face` was NOT already on `Lockstep::DrawCentered` / `DrawRight` / `CenterTextY`**, which this
document said it was. They took a scale and nothing else. They have one now, in the same position
as the renderer's.

**Two sites draw either a sentence or a datum depending on a flag, and their face has to follow
the string rather than the site.** The lobby footer draws `refused ? m_refusal : summary` — `That
is your seat. Take another one first.` against `6 SEATS - A MATCH NEEDS AT LEAST 4` — so the flag
that already picks the colour now picks the face. The connection dialog was worse: its body is a
list of paragraphs that gets wrapped into lines before anything is drawn, and `MATCH FINISHED`
ends that list with `3RD OF 6`, a placing. By the time a paragraph is lines, the draw site can no
longer tell which block a line came from, so the body carries a `Paragraph{text, face}` instead of
a bare string and wrapping preserves it. **The test found that one**, on its first run, before it
had ever been looked at on screen.

---

## Where to pick this up

**Branch `font/plex-faces`, eleven commits, tree clean, all three checkers green, 517 tests
passing.** Stages 0 to 5 are built. **Stage 6 is next and nothing blocks it.**

What stage 6 is: re-derive the layout for a 17px line box, then settle from a screenshot the three
things arithmetic cannot — the gamma on a dark background, hinted against unhinted stems, and
whether the weights are distinguishable at the final size.

**The weight assignment is stage 5's proposal, not a finding, and stage 6 is where it is judged.**
ADR-074 says three mono cuts exist to separate a card title from a rail row from a countdown, so
that is literally what was done: `MonoRegular` for rail rows, statuses, chips, counts and button
labels; `MonoMedium` for the things that name a block — a card title, a sheet title, a dialog
title, the wordmark; `MonoSemiBold` for the countdown alone. Sans went the same way: `SansRegular`
for body sentences, `SansMedium` for the two amber blocks that explain why the controls are dead
right now. **If a difference is invisible at the final size, ADR-074's instruction is to drop the
cut rather than keep it.**

**The checkable rule is not the one this plan proposed, and the difference matters.** The proposal
was: no sans string carries a digit, and no mono string ends in `.` or `?`. The first half is
false on ADR-074's own face list. The rail's help line is sans by name, and at the lock it reads
`Resolving T47. Controls return with the new digest. Anything you tap now is an order for T48.` —
a sentence, in sans, full of numbers. An event card's detail lines and a sheet row's second line
are the same shape: `Unanswered for 3 more tick(s)` is prose about a quantity. The rule's two
clauses overlap, and where they do the sentence wins — *contains a number* picks out text that IS
data, not a sentence that mentions some.

So `Tests/LockstepTests/FaceRuleTests.cpp` asserts what actually separates the two faces here:
**data on these screens is SHOUTED**, which is what `Uppercased()` is for, so a sans string with no
lowercase letter in it is a label that took the sentence face. The mono half keeps the full stop
and drops the question mark, for a reason of the same kind: `FIRST MATCH?` is a two-word shouted
prompt that aligns with the button beside it. A question mark ends a sentence and also ends a short
prompt; a full stop only ever ends a sentence.

A third test asserts that **every screen reaches both families**, per screen rather than pooled,
because the other two check nothing on a screen that draws only one face — and the connection
dialog is nearly all prose while the map is nearly all labels, so a pooled count would stay
comfortably non-zero through an entire page being reverted.

**One thing is known-wrong on screen right now and is stage 6's, not a defect to chase:** the
layout still has the 8×8 font's vertical rhythm, so a 17px line box sits in slots cut for 8px and
labels crowd their fields. Sentences are now in Plex Sans beside it, which makes the crowding
easier to see rather than worse.

**To see the main page** you need a server: run `x64\Debug\Lockstep.exe --serve --tick 4 --bots 5`,
read a token out of `x64\Debug\lockstep-<port>.store` (they are words — `charlie`, `foxtrot`), then
`Build\Screenshot.ps1 -Exe x64\Debug\Lockstep.exe -Arguments "--join 127.0.0.1:7341 --token charlie"`.
The default launch shows the join screen, and `--phase0` does not skip it. **A byte-identical capture
of the main page is not obtainable**: the match advances on a schedule the store pins to the tick
seconds it was created with, so two captures are never at the same tick, and `--tick 3600` on a
resumed store does not freeze it. Run the screenshot script under `powershell.exe`, never `pwsh`.

---

## 0. What you are starting from

**One font, hand-typed, fixed-pitch, one bit a pixel.** `NeuronClient/Font.h` is 768 bytes: 96
glyphs of printable ASCII, 8×8, indexed `code - 32`. `FontRenderer::Create` unpacks it into a 768×8
`R8_UINT` atlas and `DrawText` appends six vertices a glyph that read it with `Load()`. No sampler.

**The layout code already asks the font for measurements, and that is what makes this affordable.**
Measured 2026-09-13: 73 lines outside `FontRenderer` name a font metric, and nearly all call
`MeasurePixels`, which is a multiply today and a sum later with no edit at the call site.
`FontRenderer.h` explains why in as many words — "a stray `* 8` somewhere else is how those drift
apart" — and whoever wrote that saved this plan about fifty-five edits.

**The fixed-pitch leak is eighteen places. This is the list; do not go looking.**

| | File | Lines |
|---|---|---|
| `FitCharacters` | `Lockstep/SeatsPage.cpp` | 548 |
| | `LockstepClient/ConnectionDialog.cpp` | 216 |
| | `LockstepClient/JoinPage.cpp` | 328 |
| | `LockstepClient/MainPage.cpp` | 939, 1219, 1288, 1838 |
| `Wrap` by character count | `Lockstep/SeatsPage.cpp` | 561, 590, 624, 642 |
| | `LockstepClient/ConnectionDialog.cpp` | 225 |
| | `LockstepClient/JoinPage.cpp` | 331 |
| | `LockstepClient/MainPage.cpp` | 872, 882, 1263, 1291, 1838 |

Plus eight assertions in `Tests/NeuronClientTests/NeuronClientTests.cpp` around lines 241–298.
`MainPage.cpp:882` wraps to `_columns - 2` — a two-character indent expressed in columns, which
becomes a pixel inset. `MainPage.cpp:1838` nests a `FitCharacters` inside a `Wrap` and is the only
site where both leaks appear together.

**The face-selection surface is 81 `DrawText` calls**, measured 2026-09-13: `SeatsPage.cpp` 26,
`MainPage.cpp` 32, `JoinPage.cpp` 15, `ConnectionDialog.cpp` 3, `MapRender.cpp` 3,
`DesignTokens.cpp` 2. The last two are `DrawCentered` and `DrawRight`, which every screen routes
through — so the real count of *decisions* is 79 plus whatever those two are asked to do.

**What is already in your favour.** Every `std::string` here is UTF-8 and `NeuronCore/Text.h` owns
that. `FontRenderer::CreateHeadless` makes layout drivable from `LockstepTests` with no device.
Blending is already enabled for the interface passes (ADR-014), so Stage 3 turns nothing on.
`Build/Screenshot.ps1` captures the client area correctly — read its header, and run it under
`powershell.exe`, not `pwsh`.

**What is not in your favour.** As of 2026-09-13 this machine has no IBM Plex installed, no
`fontTools`, no `Pillow` and no `freetype-py`. Stage 1 starts by fixing that.

---

## 1. What "done" looks like

- `NeuronClient/Font.h` is written by `py Build/BakeFont.py` from `Build/Fonts/*.ttf`, and nobody
  has typed hex into it.
- `Build/CheckProjectFiles.py` fails when the recorded source, baker and content hashes disagree.
- `FontRenderer` has a `Face`, per-glyph advances, a codepoint index, and no `FitCharacters`.
- Every one of the 81 draw sites names a face, and the data/sentence rule holds — asserted, not
  eyeballed.
- `·`, `−`, `–`, `→` and `›` draw as themselves.
- Three checkers pass, Debug|x64 builds, all five suites pass.
- `Design/UI/DESIGN-GUIDELINES.md` §Font describes what is actually in the binary, and
  `Design/UI/screens` has been retaken.

---

## 2. The work, in order

One commit per stage. Do not compose them.

### Stage 0 — Width-based measurement. Invisible.

Still the 8×8 font, still fixed pitch, still one bit. **The screens must come out byte-identical.**

Delete `FitCharacters`. Make `WrapToWidth(text, widthPixels, …)` the only wrap entry point and have
`Wrap`'s internals measure with `MeasurePixels` rather than counting characters — arithmetically a
no-op today, and the whole point tomorrow. Convert the eighteen sites in §0's table.

Rewrite the eight `NeuronClientTests` assertions against widths. Keep what they actually test —
breaking on spaces, hard-breaking an over-long word, a zero-width line producing nothing — and drop
only the `== 31` arithmetic, which was a fact about 8×8 rather than about wrapping.

**Verify:** capture the four screens before and after, compare the PNGs byte for byte. Identical, or
a conversion was wrong. Then the three checkers, Debug|x64, five suites.

### Stage 1 — The toolchain and the baker, proved on the font you already have. Invisible.

Install the offline toolchain — `py -m pip install fonttools freetype-py` (or Pillow; the baker
picks one and pins which in its provenance comment). This never enters the tree: R14 binds what the
executable links, not the toolbox (ADR-073).

Write `Build/BakeFont.py`: TTF in, `NeuronClient/Font.h` out. Emit, as `constexpr` arrays under
R3's `UPPER_CASE`, per face: the glyph coverage bytes, a per-glyph advance, an offset table, a
codepoint-to-index table, and the face's cell size, ascent and line height. Write the SHA-256 of
each source TTF, of the baker, and of the generated content into the header's provenance comment,
and add the three-hash gate to `Build/CheckProjectFiles.py` — standard library only, so CI needs no
rasterizer.

**Bake today's 8×8 font first, through the new pipeline, and require the output to be byte-identical
to what is committed.** Convert `Font.h` to a one-bit source once — by hand or a throwaway script —
then bake it back. That round trip is the test that the baker is correct, and it is available
exactly once, before the face changes. Take it.

**No `.vcxproj`, no `.filters`, no `.gitignore` changes.** If you are editing one, you have drifted
into ADR-073's Option C, which the owner rejected on 2026-09-13.

### Stage 2 — `Face`, proportional metrics and codepoints. Still drawing the 8×8. Invisible.

Add the `Face` enumerator and thread it through `DrawText`, `MeasurePixels`, and
`Lockstep::DrawCentered` / `DrawRight` / `CenterTextY` — a measurement and the draw it positions
must not be able to disagree about which face they meant. Default every call site to the one baked
face so nothing moves yet.

Per-glyph advances into `MeasurePixels` (a sum; keep it `constexpr` — the tables are). `GlyphIndex`
becomes a codepoint lookup instead of `code - 32`, keeping its safety property: anything absent maps
to index 0 and draws a space rather than reading off the end of an array. Decode UTF-8 on the way
in, through `NeuronCore/Text.h`. The atlas becomes a fixed-cell grid.

**`TextVS.hlsl` does not change.** A quad's corners already carry their own texel coordinates, so a
variable glyph box is decided on the CPU and the shader never learns about it. If you are editing
it, re-read this paragraph.

**Verify:** byte-identical screenshots again.

### Stage 3 — Coverage becomes alpha. Still the 8×8 face. Still invisible.

Atlas `R8_UINT` → `R8_UNORM`. `TextPS.hlsl` loads coverage, discards at zero, and returns the colour
with its alpha multiplied by coverage. Blending is already on for this pass (ADR-014), so nothing is
switched on here.

**This stage is invisible precisely because the face is still one-bit** — every coverage byte is 0
or 255, so the output is unchanged. That is what makes it testable in isolation, and it is the last
byte-identical checkpoint in the plan. Do not skip it by folding it into Stage 4.

### Stage 4 — Bake IBM Plex. The screen changes.

Fetch the five cuts from [IBM/plex](https://github.com/IBM/plex) (OFL-1.1): Plex Mono Regular,
Medium, SemiBold; Plex Sans Regular, Medium. Bake all five into `Font.h`, subset to ASCII plus `·`
`−` `–` `→` `›`. Leave every call site on `MonoRegular`.

The screen will be readable and wrong — one face, wrong sizes, wrong line heights, probably too
thin. That is the expected state. Do not start fixing layout here; that is Stage 6.

**Print the metrics on the first bake and read them.** Plex Mono's advance is a fixed fraction of
its em and this plan does not assume which — if it is around 0.6em then at 13px it advances 7.8px,
*narrower* than today's 8px while being far more legible, which is the direction that helps ADR-014's
six shortened strings. Measure it; do not take that arithmetic on trust.

### Stage 5 — Apply the data/sentence rule to the 81 sites. **Done 2026-09-13.**

ADR-074's rule: aligns with something or contains a number → mono; a sentence with a full stop or a
question mark → sans. Worked file by file, largest first.

**What is in sans, and it is a short list**: the rail's help line, an event card's detail lines and
its verdict detail, a sheet row's second line, the sheet's "why nothing here does anything", the
lobby's four explanations of what a seat is, the two lines under `FIRST MATCH?`, the join screen's
paragraph about what a token is, a refusal that came back over the wire, and every dialog
paragraph. Everything else on all four screens is data and stayed mono — `MapRender.cpp` entirely
so, because nothing on a map is a sentence.

The rule is checked rather than remembered: `FontRenderer::DrawnStrings` records what a HEADLESS
renderer was asked to draw beside the face it was asked for — captured on the way in, where the
string is still a sentence rather than glyph boxes — and `FaceRuleTests.cpp` asserts over it. See
"Where you pick this up" for why the assertion is not the one proposed above.

### Stage 6 — Re-derive the layout, then tune what only the eye can judge.

Every hard-coded width and row height on four screens. The digest rail's "31 characters a line" is
retired. `Design/UI/DESIGN-GUIDELINES.md` §Font is rewritten; `Design/UI/screens` retaken.

Three things here are tuning, not arithmetic, and all three will look like bugs first:

- **Gamma.** Coverage blended linearly onto a dark background renders text thinner than it should.
  Expect to apply a gamma adjustment to coverage before it becomes alpha, and expect the first
  screenshot to look wrong in a way that is not the rasterizer's fault.
- **Hinting.** A bake parameter. Unhinted stems at 12–13px are softer; hinted are crisper and less
  faithful. Try both and look.
- **Weights.** ADR-074 says three mono cuts exist to carry a signal. If Medium and SemiBold are
  indistinguishable at the final size on this background, drop one — a difference nobody can see is
  not a signal, and five cuts is not a requirement.

`Uppercased()`'s comment says "the font has one case", which stops being true here. Fix the comment.
Whether the screen keeps shouting its labels is a design question ADR-074 deliberately leaves open —
do not answer it silently while editing the comment.

### Stage 7 — The copy ADR-014 could not carry.

Restore the five substituted characters in `Lockstep/MatchFixture.cpp` and re-measure the six
shortened strings. `TICK 47 LOCKS IN` was shortened because the top bar was 63px over the frame at
8px; whether it still is depends entirely on Stages 4 and 6, so **measure, do not assume**.

The `▶` replay glyph stays geometry. It was a triangle in the reference too, and ADR-014's reason
for drawing rather than typing it has not changed.

---

## 3. Things that will tempt you, and the answer

**"Let me see Plex on the screen first, then do the plumbing."** Stages 0–3 are each provable by a
byte-identical screenshot, and that test dies the moment the face changes. Spending it early buys an
afternoon and costs the only objective check this plan has.

**"Stage 3 is three lines, fold it into Stage 4."** Those three lines change the atlas format, the
shader and the blend path. Folded into a face change, a mistake in them is indistinguishable from
Plex looking odd — which is exactly the debugging session the ordering is designed to prevent.

**"The rasterizer should run in CI so the header can't go stale."** That is ADR-073's Option C with
extra steps and it makes every CI run depend on a Python font library. The three-hash gate catches a
swapped font, an edited baker and a hand-edited header, with the standard library alone.

**"While I am in here, let me improve the wrapping."** No. Stage 0's value is entirely in the
screenshots being identical. Propose it after Stage 3.

**"Uppercase everything looks wrong in Plex Sans — let me switch the labels to mixed case."** That
is a design decision ADR-074 explicitly left open, not a consequence of this plan. Raise it; do not
take it.

---

## 4. Open questions this plan leaves to the session

**The final sizes.** Mono at 12, 13 or 14px and Sans beside it, taken from a screenshot of the real
screen rather than from arithmetic. Stage 6.

**Where the OFL notice lives.** ADR-073 and ADR-074 both name it and neither answers it. Five cuts
of a licensed typeface baked into a shipped binary is unambiguously distribution, and R13 leaves
nowhere beside the executable to put a licence file — so it needs a version-resource string or an
about line. **Owner decision, and it blocks shipping rather than building.**

**Whether the subset should go beyond ASCII plus five.** Costs only atlas area and bake time. No
localization is on any roadmap, so widening it now would be building for a requirement nobody has
stated.
