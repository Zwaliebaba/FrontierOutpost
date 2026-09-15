# ADR-113 — The rail lists orders, and then places

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner, in the place-sheet interaction handoff of 2026-09-14
(`design_handoff_place_sheet/`, frame `1a`, right-hand rail) and the prompt that carries it.
**Supersedes:** — (amends ADR-086's sections and ADR-060's row)

---

## Context

The locks rail answers one question — *what goes in when the clock hits zero* — and it answers it in
four sections, two of which are order kinds: `FLEETS` (every fleet you own, grouped under a band per
system they stand at, with the ones in transit under an `UNDER WAY` band, ADR-086) and `BUILDS`
(what is queued this tick, then what is already rising, then a line saying what the purse has left).
`SIGNALS` and `PROPOSALS` follow. Every row is a link to what it names and gives no order (ADR-060).

Three things are wrong with that once a sheet is about a place (ADR-112).

**The sections are the shape of the composing code rather than of the question.** A player scanning
for what this lock will take reads two lists and has to merge them; a player scanning for what it
will *not* take reads neither, because a fleet with no order is drawn exactly like one with a move
queued — `FLT 3 · 3` with `+DEF` or nothing on the right.

**There is no way to take an order back from here.** The rail is the one surface that shows the
whole queue, and taking a build back means tapping its row, waiting for the sheet, finding the tile
and tapping that. A move is worse: the row opens a picker, and picking the system the fleet is
already standing on is the only way to cancel.

**And `FLEETS` is now a second door to a place.** The fleets standing at Dothan are on Dothan's
sheet (ADR-112), so a `DOTHAN · 10 SHIPS` band on the rail with a row under it per fleet is the same
list in two columns.

## Options considered

### A. Keep the four sections and add an `×` to each

The smallest change: `FLEETS` and `BUILDS` stay, each row grows a take-back cell. It leaves the
merge problem and the second door, and it puts the same cell on two rows that look nothing alike.

### B. `ORDERS` — one list, one row shape — and `PLACES` in place of both sections

One row per order whatever its kind: an owner square, what it is, where it is, the number, and a
44-pixel `×`. Then a `PLACES` section, one row per system you hold, carrying what it yields, what is
standing on it and how much of this tick's queue is about it.

It costs the grouping ADR-086 introduced and the reason it was introduced — ten rows reading
`FLT 13 10 HOLD HOLLIS` is the system name repeated ten times — and it costs every capture with the
rail in it.

### C. `ORDERS` only, and no second section

The shortest rail. It loses the empire summary entirely: the count of systems, the yields, and the
route to a place with nothing queued on it, which is exactly the place a player wants to reach.

## Decision

**B.** The rail is `ORDERS` and then `PLACES`, and the header says which.

**`ORDERS`.** The header above the help line reads `ORDERS · T7` with the `UNLOCKED` / `LOCKED` chip
beside it, and the help line becomes *What goes in when the clock hits zero. Tap a row to open the
place it is about.* The rows follow it with no section band of their own, in this order: what this
lock will take (queued builds, then queued moves), then what an earlier lock already did (rising
builds, then fleets under way), then what has not been ordered at all. Under them, when the queue
has taken credits, `80 CR LEFT AT THE LOCK` (ADR-053).

Five things inside that had alternatives.

**1. One row shape, and the `×` is its own 44-pixel cell.** An owner square, the title in mono
Medium (`SHIPYARD L1`, `FLT 1 → FAROE`), the place or the count in muted (`DOTHAN`, `10 SHIPS`), the
number in blue (`−20`, `T1`), and the cell. The row's body is the link and the cell is the
take-back, and the body's rectangle stops where the cell's begins so neither can swallow the other.
**The two take-backs are two actions, not one**: a build row and a fleet are different arrays, and
one index that meant either would be an index that reads the wrong one (ADR-057).

**2. The place is dropped whole when the row is too narrow, and never clipped.** 198 pixels of text
room at the 7-pixel mono column is 28 characters; `MINING STATION L2` alone is 17 of them. The title
is what the row IS, so it stays and the place goes — the top bar drops its census the same way
(SCREENS.md 01).

**3. A row for what an earlier lock already took, and no `×` on it.** ADR-070 put rising builds on
this rail because it is the receipt of everything the player has committed to, and that is still
true; what is not true of them is that this lock will take them, and nothing can take them back. So
they are rows with a muted `T14` and no cell, and fleets under way are the same shape.

**4. A fleet with no move is a dim row behind a dashed square, and it is still a target.** It is the
one thing this column never said. The handoff's own rail text calls the row "dim, non-target" and
its trigger table lists *tapping the fleet's row in ORDERS while unordered* as a way into the move,
which cannot both be true; the table wins, because a row that says *you have not ordered this* and
cannot be acted on is a reproach rather than a control. **The dashed square is a MARKER and not a
control's border**, so ADR-111's *dashed is never a target* is untouched: that rule is about the
border of a control, and this is the 8-pixel owner square saying no order has been given.

**5. `PLACES` is one row per system you hold**: a 10-pixel disc in the owner's colour, the name,
`+6 · 10 SHIPS`, and `1 ORDER` in blue or `—` in dim. A disc rather than the square an order row
wears, because on this screen a place is round and a fleet is not — the map has drawn them that way
since ADR-079. The row opens the place, and at the lock it focuses like every other row here
(ADR-060).

**The rows go on shouting.** The handoff draws `Shipyard L1` and `Dothan` in sentence case. ADR-099
took the shouting off card titles only and left it on "chips, section headers and status words";
every label on this rail is one of those, and a rail whose rows stopped shouting would be the only
surface on the screen that had. The handoff names the copy rather than the case, and the case is a
guideline it does not argue against.

## Consequences

**The rail is shorter on a busy board and longer on an empty one.** Ten fleets standing at one
system were ten rows under a band; they are ten rows without one, and the band's total moved onto
the `PLACES` row for that system. A player with nothing queued now sees a row per fleet saying so,
where the column used to read `- nothing queued -` and stop.

**`ADR-086`'s grouping is gone and its argument still stands.** The system name IS repeated down the
`ORDERS` list — `SHIPYARD L1 · DOTHAN`, `FLT 1 → FAROE · 10 SHIPS` — and what pays for it is that
the rows either side of it are a different kind of order. The band it replaced could only group one
kind.

**`TilesOn` in `TapTests` had to learn a third column.** The rail's `×` carries `ToggleBuild`,
which is correct — it is the same order taken back by the same guard — and it meant a test looking
for a build tile by action found a rail cell. It filters by pane now.

**Every capture with the rail in it is stale**: `01-main-page.png`, `01-orders-queued.png`,
`01-fleet-under-way.png`, `01-rail-hover.png`, `06-at-lock.png`, `01-finished.png`.

## What this changes elsewhere

- **Design/:** `UI/DESIGN-GUIDELINES.md` §Components (the rail's rows and its bands);
  `UI/SCREENS.md` 01 (the rail). Done in this commit.
- **Code:** `LockstepClient/MainPage.cpp` (`OrderRow`, the `orderRow` and `placeRow` lambdas, the
  header and the help sentence, the sections); `MainPage.h` (nothing new — `CancelFleetOrder` and
  `OpenSystem` arrived with ADR-112).
- **Tests:** `LockstepTests/TapTests.cpp` (`LocksRailTapTests` gains the take-back cell, the
  unordered row and the `PLACES` row; `TilesOn` and the page band's wording).
- **AGENTS.md:** nothing.

## Open questions

**Whether `ORDERS` needs a cap and an overflow line.** Every other list on this screen has one; this
one is as long as the player's fleets and queue, and the rail scrolls (ADR-101), so nothing is lost
— but a twenty-row `ORDERS` pushes `PLACES` below the fold on every frame, and the page band names
only the first section down there.

**Whether a rising build and a fleet under way belong in `ORDERS` at all.** They are receipts rather
than orders, and a section called `ORDERS` carrying things this lock will not take is a small lie the
muted number is asked to correct. The alternative is a third section, which is the shape this ADR
just removed.

**Whether `PLACES` should list systems you have seen and do not hold.** It lists what you hold,
which is what you can order at; the map is the other list, and it is the one with the fog in it.
