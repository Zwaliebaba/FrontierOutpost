# ADR-078 - A build sheet says what the queue has already taken

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Owner decision, on a screenshot of a practice match at T3: *"when you look at the build menu, It says I need 4 more CR to build something with 30 CR, but according to the top I have 46 CR. That is not correct."*
**Supersedes:** - (amends ADR-053's `NEED n MORE`)

---

## Context

Every number on that screenshot is right, and together they read as an error.

The top bar says `46 CR`. The build sheet over Pell offers `Mining station L2 - Pell` at
`30 CR - NEED 4 MORE`, dim and not a target. The arithmetic behind it is ADR-053's and it is the
arithmetic `Match::Validate` applies: a shipyard for Pell at 20 credits is already in the queue, so
`MainPage::BuildShortfall` prices the mining station against `46 - 20 = 26` and reports the four it
is short. `MatchState::CanAffordBuild` refuses the tap by the same running total, which is what
stops the client queueing a build the lock would refuse.

What the sheet never said is that the queue exists. The subtraction happens on the other side of the
screen: the locks rail's BUILDS section carries `QUEUED -20` on the queued row and
`- 26 cr left at the lock -` under it. A sheet is drawn against the bottom of the map pane, 620px
wide, and a player reading a dim row on it is looking 400 pixels away from the only line that
explains why it is dim -- in a column they opened the sheet to stop having to scan.

ADR-053 chose `NEED n MORE` deliberately and it is the right words for what is missing. The defect
is not the phrase; it is that the sheet quotes a shortfall against a purse it never names, beside a
price, under a bar naming a different purse.

## Options considered

### A. Price the row against the whole purse instead

Make `30 CR - NEED 4 MORE` agree with the top bar by dropping the running total from the shortfall.
It removes the contradiction by making the sheet wrong: the lock refuses against the running total
(ADR-053, and `Match::Validate` before it), so a row that says a build is affordable because the
purse covers it alone is a row whose order arrives refused a tick later. This is the bug ADR-053 was
written to remove and it is not an option; it is listed because "make the two numbers agree" is the
obvious first move and this is the wrong direction to make them agree in.

### B. Put the effective purse in the sheet's header

`BUILD - PELL` on the left, `26 CR` on the right. It is compact and it is where a header's status
belongs. The header's right-hand end already holds the close target -- 36 pixels of it, which must
not have anything drawn into it -- and the `LOCKED` chip sits inboard of that at the lock. A third
thing in that corner is a crowded corner, and a bare `26 CR` still does not say why it is not 46.

### C. Change the row's right-hand column to name what is left

`30 CR - 26 LEFT` instead of `30 CR - NEED 4 MORE`. It puts the number that matters in the place the
eye is already scanning, and it loses the thing ADR-053 put there: what is MISSING. `NEED 4 MORE` is
a quantity a player can go and get; `26 LEFT` is a fact they have to subtract from. It also repeats
the same figure on every row of the sheet.

### D. A sentence above the rows, in the slot the lock sentence already uses

`DrawPanel` reserves a wrapped help line under the sheet header, drawn in amber at the lock to say
why nothing on the sheet is a target (ADR-065). A build sheet whose queue has taken part of the
purse is the same shape of statement about the same sheet, and the slot is empty the rest of the
time. It costs a line of vertical space on the sheets that carry it and nothing on the ones that do
not.

## Decision

**D.** A build sheet whose queue has already taken credits says so, above its rows, in the slot the
lock sentence uses: *"Priced against the 26 credits left after the 20 already queued, not the 46 in
hand."* It names all three numbers, so the top bar, the rail and the row can all be reconciled
against it without leaving the sheet.

**It is drawn only when something is queued.** With an empty queue the top bar's purse is the whole
answer and a line repeating it is noise on every sheet a player opens.

**Amber only when it is the reason something here is not a target**, and `TEXT_DETAIL` otherwise.
Amber on this screen means "this is why nothing works" -- it is the lock sentence's ink -- and a
queue the purse still covers has stopped nothing. The sheet knows which case it is in because it has
just composed its own rows and knows whether any of them came out dim.

**The lock sentence still wins the slot.** At the lock nothing on the sheet is a target for a reason
that outranks the purse, and two sentences in one slot is a slot that has to choose.

**`NEED n MORE` is unchanged**, on the sheet row and on the digest's build button. The phrase is
right; it was the missing sentence above it that was wrong. The digest button gets no equivalent,
because the digest column is 400px wide and drops a button that does not fit rather than wrapping it
-- and the queued build's own button is on that column already, reading `- QUEUED`, which is the
same fact in the place the digest has room for it.

## Consequences

- **A build sheet with a queue is a wrapped help line taller.** Measured in the client on
  2026-09-13: the sentence takes two lines at the sheet's 596px text width, so a six-row sheet is
  36 + 46 + 264 + 40 = 386 pixels of the 676-pixel map pane. More than half the pane is still map,
  which is the constraint ADR-052 set on a sheet, and two lines is what the lock sentence takes in
  the same slot.
- **The sentence is composed by the page and not by the server.** Every other sentence that names a
  price on this screen comes from the snapshot (ADR-053's refusals, `BuildRow::detail`), and this
  one is client arithmetic over numbers the snapshot carried. It is the same arithmetic
  `CanAffordBuild` already does and the same one ADR-053 licensed the client to do; nothing new
  about the rules has moved to the client.
- **`MainPage::PurseSentence` is public and pure**, for the reason `FormatCountdown` is: the
  arithmetic is what must be right, and asserting it must not need a screen. That is one more
  method on a class whose public surface is mostly drawing.
- It does not help the digest's build button, which can still read `NEED 4 MORE` with the
  explanation on a sheet the player has not opened. The `- QUEUED` button beside it is the hint, and
  a 400px column is why that is all it gets.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01 -- the Build sheet's entry gains the sentence;
  `DESIGN-GUIDELINES.md` records that the sheet's help slot has two occupants. Done in this commit.
- **Code:** `LockstepClient/MainPage.{h,cpp}` (`PurseSentence`, and the help slot in `DrawPanel`).
  Done, built and run.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `PurseSentenceTests` pins that an empty queue says nothing, that the owner's own
numbers -- 46 in hand, 20 queued -- produce a sentence carrying 46, 20 and 26, and that a queue
larger than the purse reports nothing left rather than wrapping an unsigned subtraction.

Run on 2026-09-13 against the real client, a practice match at `--tick 90` driven by real taps: with
the shipyard queued the sheet for Dothan reads *Priced against the 80 credits left after the 20
already queued, not the 100 in hand.* under its header, in `TEXT_DETAIL`, over a `QUEUED` row and a
`15 CR` one. **The amber branch was not photographed** -- a practice match's opening purse covers
everything its sheets offer, so no row on it comes out dim -- and it is the same `atLock`-or-dim
ternary the lock sentence already takes its amber from.

## Open questions

**Whether the locks rail's BUILDS header should stop saying the raw purse.** It reads
`2 AVAIL - 46 CR` above a queued row and a `- 26 cr left at the lock -` line, which is the same two
numbers in the same column and the only place they are adjacent. Changing it is a change to a header
that has read that way since ADR-053 and it is not what the owner reported.
