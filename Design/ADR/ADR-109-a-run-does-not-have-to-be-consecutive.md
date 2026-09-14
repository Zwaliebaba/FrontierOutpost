# ADR-109 — A run of repeats does not have to be consecutive

**Status:** Accepted

**Date:** 2026-09-15
**Decided by:** Owner decision, on `08-missed-digests.png`: *"fold across breaks, first occurrence."*
**Supersedes:** — (amends ADR-062, and answers its second open question)

---

## Context

ADR-062 folds a run of repeated events under `SINCE YOU LOOKED` into one card carrying the total and
the window's span, so that four ticks away read as a summary rather than as a transcript. It bounded
the fold three ways, and one of them was this: *"A run is consecutive, same kind, same actor, same
system, same title. Consecutive in the concatenated list, which is per-tick blocks oldest first, so
anything that happened between two repeats breaks the run and the run is a genuine stretch of
nothing else happening."*

**On the board the fold was written for, that condition is almost never met.** The concatenated
window is per-tick blocks, and a tick that produces income also produces everything else that
happened in it. So tick 16's production line is followed by tick 16's buildings and claims, and only
then by tick 17's production. Two repeats are adjacent only when a tick reported nothing else at
all — which is the quiet board, not the one somebody comes back to after eight ticks away.

**Measured on 2026-09-14**, from a client that missed eight ticks of a six-seat match: the digest's
page band read `18 MORE · 18 INCOME` over two visible `Production +54` cards, so the window carried
**twenty income cards and folded none of them**. That is the failure ADR-062 exists to remove — "about
170 pixels of a 676-pixel column spent saying one thing four times, in front of the reader who has
the most to catch up on" — surviving the decision that was supposed to remove it.

It was reported as a defect and it was not one: the code does exactly what ADR-062 specifies.
`MergeRepeats` runs before ranking, over the raw list, and the sort after it is stable within a
consequence rank, so the interleaving seen on screen is the interleaving the fold saw. What was
wrong was the rule.

## Options considered

### A. Leave it, and treat the consecutiveness rule as correct

The window stays a strict transcript: cards appear in the order the ticks produced them, and a fold
only ever collapses a stretch where genuinely nothing else happened. It is the most literal thing
the client can do and it is what ADR-062 chose deliberately.

It costs the whole point of the fold. Twenty income cards is the measurement, and a decision that
fires on the quiet board and not the busy one is a decision that fires when it is not needed.

### B. Fold across breaks, card at the run's FIRST occurrence

A run becomes every event in the window that matches, wherever it sits, and the folded card keeps
the position of the earliest of them.

The cost is the one ADR-062 named: the window is no longer a transcript in order, because a card
labelled `T12 → T20` sits among tick 12's events while carrying tick 20's numbers. What makes that
acceptable is that the concatenated window is already not a narrative — it is blocks of ticks under a
header that frames all of them as one span, and ADR-062's own title for the fold is a span.

### C. Fold across breaks, card at the run's LAST occurrence

The summary sits near the most recent news, which is where a reader who scrolled to the bottom would
look for it. It is stranger on the page: a card that says `T12 → T20` sitting among tick 20's events
reads as something that happened at tick 20.

### D. Reorder the window so repeats become consecutive, then fold

Keeps ADR-062's rule by making it true. It is the largest change and the one that actually destroys
the ordering, because every event moves rather than one card staying put.

## Decision

**B.** A run is every event in the window that matches its anchor — same kind, same actor, same
system, and the same title stem — regardless of what sits between them, and the folded card keeps the
position of the FIRST of its members.

`MergeRepeats` compares each event against every open run's ANCHOR rather than against whichever run
happens to be last. The scan is quadratic in the window, which at ADR-044's eight ticks is a few
hundred events and is not worth an index.

**Every other bound of ADR-062 stands unchanged.** The fold is still gated on `unreadTicks >= 2`, so
a tick's own digest is never touched. A contact, a capture, a proposal and anything carrying a
verdict are still excluded outright. The sum still needs a sign, so `Claimed Vega 7` and `Claimed
Vega 9` are still two cards. The span is still the window's. A fold still carries every control its
members offered, once each, with at most one filled button.

**What this gives up is stated plainly:** the window stops being a transcript in order. A reader who
wants to know which tick a particular production line landed in cannot get it from the card, and
could not before either — ADR-062 already declined to stamp a `DigestEntry` with its tick, and that
is still where per-tick detail belongs.

## Consequences

**Measured on the same scenario after the change**: the window's twenty income cards became one
`Production +405 - T12 > T20` with `313 credits in hand` under it, and the page band fell from
`18 MORE · 18 INCOME` to `12 MORE · 12 INCOME` — the twelve that remain being economy events about
different systems, which are not repeats of each other and are correctly left alone.

**A folded card can now be far from the events it summarises**, and there is no line saying so. The
card's own title carries the span, which is the same claim the header four lines above it makes.

**Two runs interleaved stay two runs**, which is what the anchor comparison buys: production and
scouting alternating fold into one card each rather than into one card or none.

**Nothing changes for a player who is up to date**, which is most players most of the time, and
nothing changes for the delta box, which still counts the raw list.

## What this changes elsewhere

- **Design/:** `UI/SCREENS.md` 08 and `DESIGN-GUIDELINES.md` "Copy" — the fold no longer says
  consecutive; `08-missed-digests.png` retaken to show a fold rather than the absence of one. Done in
  this commit.
- **Code:** `LockstepClient/DigestView.cpp` — `MergeRepeats` only. `Repeats`, `SplitCount` and
  `CanMerge` are untouched, which is the point: what changed is which pairs are offered to them.
- **Tests:** `LockstepTests/MergedRepeatTests` gains `ARunFoldsEvenWithOtherEventsBetweenItsMembers`
  and `TwoInterleavedRunsFoldIntoTwoCards`. Every existing case in that class still passes unedited,
  which is the evidence that the bounds did not move.
- **AGENTS.md:** nothing.

## Open questions

**Whether a fold should say how many of the window's ticks it covers.** ADR-062 left this open and it
is sharper now: `Production +405 - T12 > T20` could be nine ticks of forty-five or two of two
hundred, and a card that reaches across breaks has more to hide than one that did not. It is a wire
field (`DigestEntry` carries no tick) and ADR-044's tabs are still where per-tick detail belongs.

**Whether the anchor should be the first or the newest member for MATCHING.** It is the first today,
so a run's identity is fixed by its earliest event. For a title stem that never varies this is the
same either way; it would matter if a stem could drift within a window, and none does.

**Whether the twelve economy cards that remain should group.** They are different systems' buildings
and are correctly not repeats, but twelve of them is still twelve cards. Grouping by KIND rather
than by repetition is a different decision and a larger one.
