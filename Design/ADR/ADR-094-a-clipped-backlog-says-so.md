# ADR-094 - A clipped backlog says so

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 3.7 at the owner's instruction to work the plan. Closes ADR-044's open question.
**Supersedes:** -

---

## Context

ADR-044 gives a returning player the whole backlog on arrival, and `NeuronServer::Session` keeps
`DIGEST_HISTORY = 8` ticks of digest per player -- a three-week match is eighty-four ticks and a
server that kept every digest for twelve players would keep a thousand.

A player away for twelve ticks gets eight. The digest's delta box then counts what it HAS: *"3
systems lost, 2 contacts"* over a header reading `SINCE YOU LOOKED · T34 → T46`. Both are true and
together they are a lie by omission -- the header claims the span and the counts cover two thirds of
it. **The gap is invisible precisely because everything on the screen is accurate.**

ADR-044 left this as an open question.

## Decision

**One muted line under the delta box when `unreadTicks` exceeds the window: `Older ticks were not
kept.`** A sentence, in the sentence face, in `NEUTRAL_DIM`, saying the one thing the box cannot.

**The client states the window rather than asking for it.** `MainPage::DIGEST_HISTORY_TICKS` is 8
and `NeuronServer::Session::DIGEST_HISTORY` is 8, and the client links neither `NeuronServer` nor
`GameLogic` (AGENTS.md §2) so it cannot read the other. The alternative is a wire field carried on
every state for one cosmetic line. If the server's window moves and this does not, the line appears
one tick early or late -- a cosmetic error about a cosmetic line.

**It is not on the wire and it is not a count.** The client does not know HOW MANY ticks were lost in
any way it could state precisely -- `unreadTicks` is derived from the tick numbers, and the server
may have kept fewer than its window for a player who was in custody. The sentence says that
something is missing, which is the part that is certainly true.

## Consequences

- **A returning player is told the report is partial**, and given no way to recover the missing
  ticks, because there is none: the server does not have them.
- **The number 8 is now written in two places**, and the two cannot be compared by any one test
  project -- `LockstepTests` links `LockstepClient`, `GameLogic` and `NeuronCore` and
  `NeuronServerTests` links neither of the first two. The test pins the client's half and names the
  server's constant in its failure message, so the grep that finds one finds the other.
- It appears only above the window, so nothing changes for the common case of a player who looks
  once or twice a day at a six-hour tick.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 08. ADR-044's open question is answered by this file rather
  than edited into it. Done in this commit.
- **Code:** `LockstepClient/MainPage.{h,cpp}`. Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `TheClientsIdeaOfTheBacklogWindowIsEightTicks` pins the constant and names the
server's in its message. All 154 methods pass.

**Not photographed** -- `08-missed-digests.png` needs a client that was away for more than eight
ticks, which needs the driven `--serve` scenario in `Design/UI/README.md`; captures are one pass at
the end of UI-01.

## Open questions

**Whether the server should keep more.** Eight is a memory decision made in `Session.h` with a
comment about a thousand digests; a player on a six-hour tick who looks twice a week is away for
about fourteen. That is a server question and this ADR only makes the current answer visible.
