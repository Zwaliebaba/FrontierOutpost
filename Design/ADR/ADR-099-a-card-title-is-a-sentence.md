# ADR-099 - A card title is a sentence, not a shout

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision, answering `Design/Plans/UI-01-ClientImprovements.md` item 3.9's [ASK]: *"Mixed case for card titles."*
**Supersedes:** - (answers ADR-074's open question)

---

## Context

Every label on these screens is uppercased, by `Uppercased()` at the call site. That was not a choice
until 2026-09-13: the 8x8 font had no lowercase at all (ADR-014). Plex has both (ADR-074), which
turned a constraint into a decision, and ADR-074 left it open.

The digest is where it costs something. A card title is `BATTLE AT ULME` over a detail line reading
*7 of 10 lost (defending)* -- a shout over a sentence, about the same event. The titles are the
longest strings on the screen and uppercase is the least legible case for a long string; ADR-084 has
just made them 16px, which makes them louder still.

## Decision

**Card titles are mixed case: `Battle at Ulme`.** Uppercase is reserved for chips, section headers
and status words -- `LOCKED`, `CAPTURED`, `QUEUED`, `+DEF`, `FLEETS`, `SIGNALS` -- where it marks a
label rather than a phrase.

**This ADR records the decision and no code changes with it**, which is what UI-01 3.9 asked for:
*"Write the ADR and stop; do not implement without the owner's answer."* The answer is in, and the
implementation is its own item because of what it drags with it.

**What it drags: `FaceRuleTests` tells a label from a sentence by looking for a lowercase letter.**
That is a real discriminator today precisely because every label is shouted, and it is what enforces
ADR-074's mono-for-data / sans-for-sentences rule across five screens. Mixed-case titles break it --
a mixed-case title in the mono face would read as a sentence in the data face and the test would
fail, correctly, for the wrong reason.

**The replacement is an explicit tag.** `FontRenderer::DrawnString` carries a face; it needs to carry
what the string IS -- label or sentence -- set where the string is composed rather than inferred from
its bytes. That is a change to every draw call the test covers, which is the item.

## Consequences

- **The screen reads less like a console.** That is the point and it is also a loss: the ops-console
  look is what the reference sheets were drawn in, and the uppercase titles are a large part of it.
  Chips and headers keep it.
- **`Uppercased()` stops being applied to titles** and stays for everything else, so the function
  survives and its comment about the shouting being a choice becomes true in a narrower way.
- **`FaceRuleTests` gets stronger, not weaker.** An explicit tag is a claim the author makes rather
  than a property of the bytes, and it catches a label that happens to contain a lowercase letter --
  a system named `McBride` today defeats the current test silently.
- Twelve captures change. They are already changing for ADR-084 and would change again for UI-02.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Copy records the decision as taken and not yet built;
  ADR-074's open question is answered by this file rather than edited into it. Done in this commit.
- **Code:** nothing yet. The item is `Design/Plans/UI-02-TouchTargets.md` §2, alongside the other
  change that touches every draw call.
- **AGENTS.md:** nothing.

## Verification

None: no code changed.

## Open questions

**Whether event DETAIL lines change too.** They are already mixed case and already sans, so nothing
about them moves; the question is whether a title in sans would then be right, and that is a face
question rather than a case one.
