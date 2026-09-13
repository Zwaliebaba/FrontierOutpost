# ADR-087 - The purse on the bar says what this tick has already committed

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision, on ADR-078: *"And on the credit topic, would it not make sense to have after the CR, and (-20) to show how much is already getting spend this round?"*
**Supersedes:** - (extends ADR-078 to the top bar)

---

## Context

ADR-078 fixed the build sheet: a sheet whose queue has taken credits now says so above its rows.
That closes the contradiction at the place a player is deciding, and it leaves it open at the place
they first notice it.

The original report is the evidence for where that is: *"It says I need 4 more CR to build something
with 30 CR, but according to the top I have 46 CR."* **The two numbers that did not agree were the
sheet's and the TOP BAR'S.** The bar is where the purse is read -- it is beside the score, in the
same weight, and ADR-053 put it there precisely because that is where the eye goes -- and it said
`46 CR` while 20 of it was spoken for.

The subtraction existed in exactly one place, the locks rail's `- 26 cr left at the lock -`, 400
pixels away in a column a player opens a sheet to avoid having to scan.

## Options considered

### A. Leave the bar alone; the sheet explains it

What ADR-078 decided, and its own Consequences admitted the gap: *"It does not help the digest's
build button, which can still read `NEED 4 MORE` with the explanation on a sheet the player has not
opened."* The same is true of the bar, and the bar is worse, because it is the number the player
trusts.

### B. Show the purse AFTER the queue: `26 CR`

Make the bar agree with the sheet by showing what is actually spendable. It removes the
contradiction and creates a different one: the production card says `46 credits in hand`, the rail
header says `2 AVAIL · 46 CR`, and the number on the bar would be neither. It also hides the purse,
which is the one figure that survives the tick whatever is queued.

### C. Show both: `46 CR −20`

The purse, then what this tick has committed of it. Nothing is hidden, the subtraction is in the
reader's hands, and it is the same shape the rail already uses for a queued build (`QUEUED −20`).

## Decision

**C.** The top bar draws the committed amount immediately after the purse, in blue, and only when
something is queued: `46 CR −20`.

**Blue because blue is what this player has committed.** It is the ink of a queued build's row on
the rail, of a queued build's button on a card, and of the `QUEUED` status on a sheet row. A fourth
use of it for the same meaning is the economy ADR-027 asked for, not a dilution of it.

**Only when something is queued.** With an empty queue the purse is the whole answer and a `−0` is a
subtraction nobody did.

**It is `QueuedBuildCost()`, the same call `PurseSentence` and `CanAffordBuild` make.** Three
surfaces now say the same arithmetic and none of them computes it: the bar subtracts it, the sheet
explains it, and the guard refuses by it.

## Consequences

- **The bar is wider by about thirty pixels while a build is queued.** It is laid out right to left
  from the replay button, so everything left of the purse shifts rather than anything being clipped;
  the census on the far left is the clause-dropping one and is what gives first.
- **Three places now say what the queue took**: the bar, the rail, and the sheet. That is repetition
  on purpose -- it is one number in the three places a player looks -- and it is only ever one call,
  so they cannot drift.
- It says nothing about a build that is *rising* (already paid for on an earlier lock). Those are
  spent, not committed, and the purse already excludes them.
- A player who queues and unqueues watches the bar move, which is feedback ADR-053 wanted for the
  button and never gave to the number.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Frame (the top bar's contents), `SCREENS.md` 01. Done in this
  commit.
- **Code:** `LockstepClient/MainPage.cpp` (`DrawTopBar`). Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `CommittedPurseTests` pins that a fresh match commits nothing, and that the number
the bar subtracts is the number the sheet's sentence reports as left -- 46 in hand, 20 queued, 26
left -- so the two cannot disagree. All 140 methods in the suite pass.

**Not photographed.** Queueing a build needs a tap and the desktop was locked for this session; the
captures are being taken as one pass at the end of UI-01.

## Open questions

**Whether the digest's build button should carry it too.** ADR-078 left that open on width grounds
and this does not change the width available. The button beside it already says `- QUEUED`, which is
the same fact for one build rather than for all of them.
