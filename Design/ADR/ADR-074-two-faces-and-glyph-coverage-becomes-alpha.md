# ADR-074 — Two faces, a data/sentence rule, and glyph coverage becomes alpha

**Status:** Accepted 2026-09-13 (owner, against a mockup of the main page in the two faces) — **amended
2026-09-13 by its own contingency: FOUR cuts ship, not five.** Plex Mono SemiBold was dropped at
FONT-01 stage 6 under the sentence in the Decision below that says to drop a cut rather than keep a
difference nobody can see; the measurements that settled it are under "What shipped" at the end. The
data/sentence rule, the two families and coverage-as-alpha are unchanged.

**Date:** 2026-09-13
**Decided by:** Owner brief, 2026-09-13: IBM Plex Mono for data, IBM Plex Sans for sentences, with a stated rule for which is which. This ADR records the decision and the one thing the brief implies without saying — that the screen acquires anti-aliased text, reversing ADR-014 for the interface's text pass.
**Supersedes:** — (revises ADR-014's rule on alpha, for the text pass only; ADR-014 is Accepted and is not edited)

---

## Context

ADR-073 decides how a font gets into the binary. This decides which fonts, and it cannot avoid a
question ADR-073 deliberately left alone.

**The brief.** Two families, five cuts:

- **IBM Plex Mono** (Regular 400, Medium 500, SemiBold 600) — the *data* face: every rail row,
  status, number, chip, button label, section label, card title, top bar, countdown, sheet row,
  legend. *SemiBold did not survive the screen; see "What shipped".*
- **IBM Plex Sans** (Regular 400, Medium 500) — the *sentence* face only: event-card detail lines,
  the rail's help line, dialog paragraphs, sheet second lines, the join screen's explanatory lines.

**The rule.** If the text aligns with something or contains a number, it is mono. If it is a
sentence with a full stop or a question mark, it is sans.

The rule is a good one and it already describes the screen. `Design/UI/DESIGN-GUIDELINES.md`
specifies an event card as "the title uppercased in primary, detail lines wrapped in body colour
and indented 18px" — a label and a sentence, differing today only in colour and indent. The brief
gives that distinction a second signal, which is what typography is for.

**What the brief implies but does not say.** IBM Plex is a vector typeface. The screen draws text at
8px. A vector face rasterized to one bit a pixel at 8px is not a smaller version of itself; it is
noise, because at that size the stems and counters of a humanist letterform fall between the
pixels that would have to carry them. This is why the faces that look good at 8px are *drawn* at
8px, pixel by pixel, and why ADR-014's font is one of those.

So the brief is reachable in exactly two ways: draw Plex much larger and stay one-bit, or keep the
text small and let a glyph pixel carry **coverage** — which is the thing ADR-011 and ADR-014
exist to keep off this screen. ADR-014 states it plainly: *"Alpha in these passes is a material,
not coverage. A card fill really is 4% white; a glyph pixel is still lit or discarded with nothing
in between."*

**The engineering cost of reversing that is close to nothing, and that is worth stating so the
decision is made on its merits rather than on a supposed difficulty.** Blending is already enabled
for the two interface passes — ADR-014 turned it on and recorded it as R12's exception. The atlas
changes from `R8_UINT` to `R8_UNORM`, and `TextPS.hlsl` changes from

```hlsl
uint lit = g_fontAtlas.Load(...);  if (lit == 0) { discard; }  return _input.color;
```

to a load of coverage, a discard at zero, and a return of the colour with its alpha multiplied by
that coverage. Three lines. Nothing else in the renderer moves — `TextVS.hlsl` is untouched, since
a quad's corners already carry their own texel coordinates.

**What it costs is not engineering but identity.** `Design/blueprint.md` §7 asks the fiction to be
"written to the ops console's tone… a setting that survives being told in 8-pixel capitals." The
8-pixel capitals are about to stop being the medium.

## Options considered

### A. IBM Plex, one bit a pixel, at 16px or larger

Honours the brief's faces and keeps ADR-014's rule intact. At 16px a thresholded Plex is legible,
if coarse.

Rejected. The screen is 1280×720 and its information density is designed around 8px text with a
12px line height — a 400px digest rail holding 31 characters a line, seven events, three columns of
orders. Doubling the type size without doubling the screen means roughly a quarter of the content
fits, and the answer to that is a redesign of the game's one screen, which is not what was asked
for. It also throws away most of what Plex is: a thresholded humanist sans at 16px looks worse than
a bitmap face drawn at 16px, so the option pays the identity cost *and* the density cost and gets
the weaker result.

### B. Two Plex families, anti-aliased, at their natural UI sizes

The brief as written. Text renders at around 12–13px with grayscale coverage, which is what these
faces were drawn for.

It costs ADR-014's rule, and it costs a re-derivation of every measurement on four screens, because
Plex Sans is proportional and Plex Mono's advance is not 8px.

### C. Decline the brief and use a bitmap face

What ADR-073's first draft proposed: one well-drawn 1-bit face such as Spleen 6x12. Cheapest by a
wide margin, preserves every recorded decision, and suits an ops console.

Recorded here because it remains the cheap answer if the cost below is judged too high — not
because the brief is in doubt. It does not deliver what the brief asked for: there is no
clean, well-licensed, UI-grade *proportional* bitmap face at these sizes, so the mono/sans
distinction would have to be carried by weight and colour alone.

### D. Hybrid — a 1-bit mono face for data, anti-aliased Plex Sans for sentences

Tempting, because it puts coverage only where sentences live and keeps numbers on exact pixels.

Rejected as a decision, though it falls out of the plan for free: once the renderer carries both
kinds of atlas it can mix them, and the Face enumerator is where that would live. It is rejected as
the *target* because two rendering models on one screen is a seam a reader can see — the digits
would be crisp and the prose soft, which reads as a defect rather than as a choice.

## Decision

**The client draws two families in five cuts, chosen per draw call by the data/sentence rule above,
and a glyph pixel carries coverage.** Option B.

`FontRenderer` gains a `Face` enumerator — `MonoRegular`, `MonoMedium`, `MonoSemiBold`,
`SansRegular`, `SansMedium` — passed to `DrawText` and to `MeasurePixels` beside the existing scale.
`Lockstep::DrawCentered`, `DrawRight` and `CenterTextY` take it too, so that a measurement and the
draw it positions cannot disagree about which face they meant.

**Coverage is alpha in the text pass and nowhere else.** The shape pass is untouched: ADR-014's
sentence that a card fill really is 4% white still holds, and so does every scene pass. What
changes is one sentence about glyphs, and it changes because the brief's faces cannot be honoured
otherwise. `SetClipRect` keeps clipping by whole glyphs.

**Weights are a signal, not decoration.** Three mono cuts exist to separate a card title from a
rail row from a countdown, and if at the final size two of them are indistinguishable, the
answer is to drop a cut rather than to keep a difference nobody can see. Verify this on the real
screen before the layout is re-derived around it. **This was verified, and a cut was dropped —
see "What shipped".**

**`Uppercased()` stops being forced by the font.** Its comment says "the font has one case"; Plex
has two. Whether the screen keeps shouting its labels is a design question this ADR does not
settle — it only removes the constraint that used to answer it.

**The rule is checkable and should be checked.** A string drawn in sans that contains a digit, or
one drawn in mono that ends in a full stop, is a misapplication of the rule rather than a matter of
taste. `LockstepTests` can assert both over the fixture strings once a draw call records its face,
and that is cheaper than re-reading 81 call sites every time the copy changes.

## Consequences

**What this makes easy.** The five characters ADR-014 substituted are all present in Plex, and the
six shortened strings get re-measured against a face whose advance is narrower than 8px at the
sizes in question. Sentences stop being label-shaped. Weight becomes available as a signal on a
screen that currently has only colour and size.

**What this makes hard.** Every measurement on four screens moves. Plex Sans is proportional, so
`MeasurePixels` becomes a genuine sum and the digest rail's "31 characters a line" stops being a
fact. The screen captures in `Design/UI/screens` are all retaken, and
`Design/UI/DESIGN-GUIDELINES.md` §Font is rewritten.

**What it costs, that is easy to miss.** Anti-aliased text on a dark background blended linearly
looks thinner than the same text on white, and the usual remedy is a gamma adjustment on coverage
before it becomes alpha. Expect to tune that, and expect the first screenshot to look wrong in a
way that is not the rasterizer's fault. Hinting is a bake parameter with the same property.

**What it forecloses.** The pixel-exact guarantee for text. ADR-011's promise that nothing on the
path resamples anything still holds for geometry and for the shape pass, and a glyph is now the one
place on the screen where a pixel's colour was decided by a rasterizer rather than by a designer.
That is the trade, stated plainly.

**What it does not foreclose.** Reverting. The faces are files and the renderer keeps both atlas
kinds working, so Option C remains one bake away.

## What this changes elsewhere

- **Code:** `NeuronClient/FontRenderer.{h,cpp}` gain `Face`, per-glyph advances, a codepoint index
  and UTF-8 decoding; the atlas becomes `R8_UNORM`. `NeuronClient/Shaders/TextPS.hlsl` changes as
  above; `TextVS.hlsl` does not. `LockstepClient/DesignTokens.{h,cpp}` thread `Face` through the
  three shared helpers. 81 `DrawText` call sites name a face.
- **AGENTS.md:** R12's blending exception is already recorded by ADR-014 and needs no change. No
  new rule.
- **Design/:** `Design/UI/DESIGN-GUIDELINES.md` §Font is rewritten and its derived "31 characters a
  line" facts retired. `Design/UI/screens` captures retaken. ADR-014 is **not** edited — it is
  Accepted and immutable — and this ADR is where a reader is sent for the alpha rule and for the
  substitution table's fate.

## Open questions

**The final type sizes.** Plex Mono at 12px, 13px or 14px, and Plex Sans beside it, is a decision
that should be taken from a screenshot of the real screen rather than from arithmetic. Note that
Plex Mono's advance is a fixed fraction of its em — measure it on the first bake rather than
assuming — so a size that is *taller* than today's 8px may still be *narrower* per character, which
is the direction that helps the six shortened strings.

**Whether labels stay uppercase.** Removed as a constraint, not answered as a design question.

**~~Whether three mono weights survive contact with the screen.~~ Answered 2026-09-13: two did.**
See "What shipped".

**Where the OFL notice lives.** Carried over from ADR-073, still an owner decision, and it binds
this ADR harder because five cuts of a licensed typeface is unambiguously distribution.

---

## What shipped

Added 2026-09-13, after `Design/Plans/FONT-01-PlexFaces.md` built this decision in seven stages.
**Everything below was measured on the built client, not argued from the brief.**

**Four cuts, not five.** Plex Mono SemiBold is not baked. Measured at 12px over `SHIPYARD L1 HOLLIS
20 CR`, as coverage after the gamma correction below:

| step | ink | fully covered pixels |
|---|---|---|
| Mono Regular → Medium | **+15.4%** | 10% → 18% |
| Mono Medium → SemiBold | **+9.1%** | 18% → 24% |
| Sans Regular → Medium | **+23.1%** | 4% → 15% |

Side by side at 1× and at 2×, the first step is obvious and the second is not. The structural
argument is the stronger one: SemiBold was set in exactly one thing, the lock countdown, which is
already the only amber on the top bar and already twice the size — a third signal on the one string
that needed none, and absent from every place two weights actually meet. The sans pair was measured
the same way and **kept**. The countdown is Mono Medium.

**Coverage is gamma-corrected before it becomes alpha, and that is not a tuning value.** The back
buffer is `R8G8B8A8_UNORM` and deliberately not `_SRGB` (ADR-011), so the blender mixes sRGB-encoded
values as though they were linear. Light on dark that lands too dark: a half-covered pixel reaches
50% of the string's brightness where it should reach about 73%, and the thinner the stem the more of
it is partial coverage — so the face read a weight lighter than the cut it was baked from, which is
the opposite of what this ADR chose the cuts for. `TextPS.hlsl` raises coverage to 1/2.2, which
against a black background is exactly what blending in linear space would have produced. A fully
covered pixel and an uncovered one are untouched.

**Hinting is on, and it was checked rather than assumed.** Unhinted at 12px produces *zero* fully
covered pixels in either family — every stem lands across two columns at partial coverage. It is a
parameter of the bake, so revisiting it is a re-bake and a screenshot.

**The subset gained a sixth character.** `‹` is baked alongside this ADR's five. The digest pager is
a matched pair — `‹ PREV` against `MORE ›` — and a control with one typeset half and one ASCII half
reads as a defect rather than as a decision.

**The rule is checked rather than remembered, and the check is not the one that was proposed.**
`Tests/LockstepTests/FaceRuleTests.cpp` asserts over what a headless renderer was *asked* to draw.
The obvious assertion — no sans string carries a digit — is false against this ADR's own face list:
the rail's help line is sans by name and at the lock reads `Resolving T47. Controls return with the
new digest. Anything you tap now is an order for T48.` Where the rule's two clauses overlap, **the
sentence wins**: *contains a number* picks out text that IS data, not a sentence that mentions some.
What is asserted instead is that data on these screens is shouted, so a sans string with no lowercase
letter in it is a label that took the wrong face — which leans on `Uppercased()`, and therefore on
the open question below.

**Still open, and untouched by any of this:** whether the screen keeps shouting its labels now that
the font no longer forces it, and where the OFL notice lives. The second blocks shipping rather than
building.
