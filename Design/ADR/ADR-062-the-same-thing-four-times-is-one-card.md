# ADR-062 - The same thing four times is one card

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, in the 2026-09-12 build prompt: *"consecutive events of the same kind about the same subject with no other consequence (today: `PRODUCTION +6` × 3) merge into one card."*
**Supersedes:** - (narrows ADR-044's concatenated window)

---

## Context

ADR-044 gives a returning player everything they missed: the server keeps eight ticks of digest and
the match loop concatenates every one newer than the tick this client last drew, oldest first, into a
single list under `SINCE YOU LOOKED - T6 > T9`.

What that produces for a player away four ticks is mostly the same card four times. Production runs
every tick, so four ticks away is four `PRODUCTION +6` cards, each with `154 credits in hand` under
it and each with the same MAP button — about 170 pixels of a 676-pixel column spent saying one thing
four times, in front of the reader who has the most to catch up on. ADR-061 pages that column now, so
the repeats no longer push a contact off the bottom; they still push it onto page two.

**The repetition is an artefact of the window, not of the game.** Within one tick, two economy lines
are two different things that happen to read alike. Across four, one line repeated is one fact whose
number moved — and the header above it already frames the whole window as a single span.

## Options considered

### A. Leave it

Every tick's digest is what the server said, unedited, and four ticks are four digests. It is the
most literal thing the client can do and it is what the paging in ADR-061 was written to survive.
What it costs is the reading: the summary of a missed window is not a transcript of it.

### B. Show only the newest of a repeated run

One card, latest number, the rest dropped. Cheapest to implement and it loses the thing the reader
actually wants, which is what the window *totalled* — `PRODUCTION +6` is not the answer to "what did
four ticks of income come to".

### C. Fold a run into one card carrying the total and the span

Consecutive events of the same kind about the same subject collapse into `PRODUCTION +18 - T6 > T9`,
detail from the newest, every control the run offered carried across.

The cost is that the client now reads the server's prose rather than only printing it: the number
has to come out of the title to be summed.

## Decision

**C**, bounded three ways.

**Only under `SINCE YOU LOOKED`.** The fold is gated on `unreadTicks >= 2` — the same condition the
header and the delta box use. A tick's own digest is never touched, so nothing a player watching
live sees is ever summarised.

**Never over a consequence.** A contact, a capture and a proposal are excluded outright, and so is
anything carrying a verdict. Two contacts are two rivals arriving and two captures are two systems
gone; the count is the whole of what those events say, and a bigger number on one card is a fact the
player was not told. What is left — income, the region's timer, a custodian's silence — is what a
window's worth of can be stated once.

**A run is consecutive, same kind, same actor, same system, same title.** Consecutive in the
concatenated list, which is per-tick blocks oldest first, so anything that happened between two
repeats breaks the run and the run is a genuine stretch of nothing else happening.

**The sum needs a sign.** `Production +6` folds because its trailing digits are introduced by `+`;
`Claimed Vega 7` does not, because the 7 is part of a name, and two titles with no signed count fold
only when they are identical. That rule is the whole defence against `Claimed Vega 14`, and today
`Production +{}` is the only title the resolver emits that ends in a number at all.

**The span is the window's, not the run's.** `- T6 > T9` is `lastSeenTick` and the current tick,
which is what the header says four lines above it. A `DigestEntry` carries no tick — nothing on the
wire does — so a per-run span would need one, and the window's span is the honest claim anyway: it
says what these ticks came to, not which of them contributed.

**A fold carries every control the run offered, once each**, with at most one filled button. A build
is offered on exactly one card (ADR-057), so dropping the second event's button would take a build
off the screen for the tick, and keeping two primaries would put two filled buttons on one card.

**The delta box is untouched.** It counts `MatchState::digest`, which is the raw list, because the
box is the tick's arithmetic and the fold is the screen's.

## Consequences

- A four-tick absence reads as a summary rather than a transcript, and the cards that survive it
  whole are exactly the ones that cost the player something.
- **The client parses the server's prose.** That is new and it is worth naming: the digest has always
  been strings the server authored, and this reads a number back out of one. It is confined to
  `SplitCount`, it fails closed — an unparsed title merges only with an identical one — and the
  server is still the only thing that decides what happened.
- A merged card focuses the system its FIRST event named, which is the same system for every event
  in the run by construction.
- Nothing changes for a player who is up to date, which is most players most of the time.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 08 gains the fold; `DESIGN-GUIDELINES.md` "Copy" records it.
- **Code:** `LockstepClient/DigestView.cpp` — `MergeRepeats` and the two helpers, run before ranking
  and grouping so a fold counts as one event everywhere.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `MergedRepeatTests` pins the three-into-one fold with its total and span, that a
player who missed nothing sees every line, that contacts, captures and verdicts are never folded,
that `Claimed Vega 7` and `Claimed Vega 9` stay two cards, and that a fold keeps every action once
with one filled button. `DeltaTests::MergingRepeatsDoesNotChangeTheDelta` pins the box.

## Open questions

**Whether a run should be able to say how many ticks it covers.** `PRODUCTION +18 - T6 > T9` does not
say whether that was three ticks of six or two of nine, and stamping a `DigestEntry` with its tick
would let it. That is a wire field, and ADR-044's tabs are where per-tick detail belongs.

**Whether the merge should reach across a break.** Four ticks of production with one contact in the
middle are two runs today. Folding them into one would mean reordering the window, which is the one
thing the concatenation has so far avoided.
