# ADR-102 - A sentence-cased title in the mono cut, and the tag that was not built

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Implementing ADR-099, and answering the question it left open: *"whether a title in sans would then be right, and that is a face question rather than a case one."*
**Supersedes:** - (amends ADR-099's plan, not its decision)

---

## Context

ADR-099 decided that a card title is a sentence: `Battle at Ulme`, not `BATTLE AT ULME`. It recorded
the decision and deliberately changed no code, and it left two things to the implementation.

**The face.** ADR-074's rule is *data is mono, sentences are sans*. A title that has just been
declared a sentence should therefore be sans. ADR-084 had made titles 16px, in the display cut.

**The tag.** ADR-099 says `FaceRuleTests` tells a label from a sentence by looking for a lowercase
letter, that mixed-case titles break that, and that the replacement is an explicit label/sentence tag
on `FontRenderer::DrawnString`, set at every draw site the test covers.

Both turned out differently when the code was in front of them, which is the only reason this file
exists.

## What the font actually has

`Font.h` is baked offline from the IBM Plex TTFs (ADR-073) and carries five faces:
`MonoRegular`, `MonoMedium`, `SansRegular`, `SansMedium`, `MonoDisplay`.

**There is no sans display cut.** The 16px size ADR-084 introduced exists in mono only. So a
sentence-cased title at title size has nowhere sans to go without re-baking the font -- which means
regenerating a committed 2,000-line header, and the tree carries an explicit warning against
re-baking it casually, from the day a stale-bake check took `main` red.

## What the test actually does

`FaceRuleTests` has two assertions, and only one of them uses the lowercase discriminator:

- `NothingInMonoEndsASentence` flags a **mono** string ending in a full stop. A card title does not
  end in a full stop, mixed case or not. Unaffected.
- `NothingInSansIsShouted` flags a **sans** string with no lowercase letter in it. It reads the
  signal in one direction only -- *shouted implies label* -- and every label on these screens is
  still shouted, because ADR-099 keeps the capitals for chips, section headers and status words.
  Unaffected.

So mixed-case titles did **not** break the test. The suite was green before the case change and
green after it. ADR-099's account of the dependency was written from the rule rather than from the
two assertions, and the rule is wider than they are.

## Decision

**The title stays in `MonoDisplay` and only its case changes.** ADR-074's rule is bent here, once,
for a reason that is a fact about the baked font rather than a preference: there is no sans at this
size. It is written down so that nobody re-derives the contradiction from first principles and
"fixes" it by shouting the titles again.

**The explicit tag is not built.** Two reasons, and the second is the one that matters:

- It is not needed for the case change, per the section above.
- **It would not be an independent check.** The tag would be typed at the same call site as the
  face, by the same hand, in the same moment. An author who reaches for the wrong face is an author
  who reaches for the wrong tag; what makes the current discriminator worth having is that it is
  derived from the BYTES, which nobody chose while thinking about typography. A second spelling of
  one decision, at 114 draw sites, buys the appearance of rigour and none of it.

**What is built instead is the checkable half of ADR-099.** `NoCardTitleIsShouted` asserts that no
title with words in it is drawn in capitals, so the decision cannot be reverted by putting one
`Uppercased()` back. Its discriminator is the display cut plus the screen -- and the second half is
not padding: the first run of that test said `MonoDisplay` is also the connection dialog's
`WAITING FOR THE HOST` and `MATCH FINISHED`, which are status headings and stay shouted.

## Consequences

- **One string on the screen breaks ADR-074's rule**, knowingly. The rail's help line, a card's
  detail lines and every sheet's prose are still sans; the title above them is mono.
- **The digest reads as a column of sentences** with a shouted stamp on the right, which is the
  contrast ADR-099 wanted.
- **`Uppercased()` survives** and its comment about the shouting being a choice becomes true in a
  narrower way: it is the choice for chips, headers and status words, and no longer for titles.
- **`FaceRuleTests` has a named blind spot** rather than an unnoticed one: a mixed-case string in
  the mono face is no longer necessarily wrong, so the mono half of ADR-074 is unenforced for card
  titles. The test's header comment says so and names this file.
- **If a sans display cut is ever baked**, the title moves to it and the bend goes away. That is the
  open question below, and it is a font decision rather than a screen one.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Copy (the rule as built, and the one exception),
  `Design/UI/SCREENS.md` screen 01, `Design/Plans/UI-02-TouchTargets.md` §2.
- **Code:** `LockstepClient/MainPage.cpp` (one `Uppercased()` removed, with the reason beside it),
  `LockstepClient/DigestView.cpp` (the two titles composed in the client, authored in sentence case).
- **Tests:** `Tests/LockstepTests/FaceRuleTests.cpp` (`NoCardTitleIsShouted`, and a header comment
  that says what the discriminator now does and does not catch).
- **AGENTS.md:** nothing.

## Verification

The whole suite, before and after the case change, to establish that the tag was not load-bearing.
`NoCardTitleIsShouted` over the main page at tick 12, which draws titles from several kinds of
event. A screenshot of the digest, which is the only way to judge whether `Production +6` over
`112 credits in hand` reads better than `PRODUCTION +6` did.

## Open questions

**Whether to bake a sans display cut.** It is one entry in `BakeFont.py`'s face list and a re-bake
of `Font.h`, and it would let the title be sans as ADR-074 says. Against it: the display cut is what
gives the digest its column-heading look, Plex Sans at 16px beside Plex Mono at 12px is a bigger
change to the screen than the case was, and the re-bake is the operation this tree has been burned
by. It needs somebody to look at both on a screen.

**Whether sheet titles follow.** `BUILD - DOTHAN`, `MOVE FLT 1 - PICK LANE`. ADR-099 reserves
capitals for section headers, and a sheet's title is one; they are left shouted on that reading.
`Design/Plans/UI-02-TouchTargets.md` §2 said "and sheet titles", which is the looser reading and is
not what was built.
