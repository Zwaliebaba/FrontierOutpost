# ADR-060 - A locks rail row is a link to what it is about

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, in the 2026-09-12 build prompt: *"A `BUILDS` row opens the build sheet for that system. A `FLEETS` row focuses that fleet's location on the map. A `PROPOSALS` row focuses the system the proposal is about."*
**Supersedes:** - (amends ADR-034's read-only rail)

---

## Context

ADR-034 moved every control onto the digest and left the locks rail read-only, which is a good
division and was implemented as *nothing on the rail is tappable at all*. The 2026-09-11 handoff had
asked for the other thing — "tapping a row jumps to the event that owns it" — and
`Design/UI/SCREENS.md` has carried that as a "differs" line ever since.

The cost of the read-only rail is not that a player cannot give an order from it. It is that the
rail names things the rest of the screen can show them and gives no way across. A row reads
`SHIPYARD - PELL / QUEUED -20`, and taking that build back means finding Pell on the map by eye and
tapping it, on a map the player may have orbited since. A row reads `FLT 1 10 > PELL` and the fleet
that row is about has a marker somewhere on the lane. A row reads `P3 LANE` and the border it is
offered on is not named at all.

`Ink::HOVER_FILL` has existed since the tokens were collected into one header and
`DESIGN-GUIDELINES.md` records it as "drawn by nothing yet" — because until a row was a target
there was nothing for a hover to mean.

## Options considered

### A. Leave the rail read-only

The state ADR-034 left. It is consistent — one surface for orders — and it is the cheapest thing to
keep true. What it costs is the crossing above: three lists of named things and no way from a name
to the thing.

### B. Rows become controls: unqueue a build from its row, redirect a fleet from its row

The rail becomes a second order surface. It removes the crossing by removing the need for it, and
it puts the same order in two places — which is what ADR-034 was written to stop. It also needs a
confirm step on the row for anything destructive, on a column that has none.

### C. Rows become links: a tap takes the eye to the thing the row names

A row gives no order. It focuses a system, or opens the sheet that is already the way to change the
thing the row is about. Nothing new can be ordered from the rail, and nothing that could be ordered
from it is ordered anywhere else.

## Decision

**C.** A locks rail row is a link, and what it links to is the thing it names.

A **BUILDS** row opens the build sheet for the system the queued build is on — the same sheet that
queued it, which is where it is taken back (ADR-058 keeps a build sheet about one system, so the row
has to look the system's position up from the id the `BuildRow` carries; ADR-057 is why those are
different numbers). A **FLEETS** row focuses where the fleet is standing, and a fleet under way
opens its destination picker instead, which is exactly what tapping its marker on the map does. A
**PROPOSALS** row focuses the far end of the lane the offer is about — the far end, because the near
one is this player's own and the border they have not looked at is the other. An offer about no lane
— scouting, a hold — is about no system, so that row is not a target.

**The row under the pointer is filled with `Ink::HOVER_FILL`**, which is what the token was for. It
is drawn only on rows that are targets, so the fill is the answer to "is there anything here", and
the page redraws when the pointer crosses from one row to another rather than whenever it moves
(ADR-047). `PointerInput` gains the position, read rather than taken: a hover is a state, and a
finger reports one only while it is down, so nothing on the screen may depend on it.

**The hover position comes from `WM_MOUSEMOVE`, and that is a measured exception to one input
path.** MVP-01 section 2 and `PointerInput`'s own header say mouse and touch arrive through one
channel, which `EnableMouseInPointer` gives for every button. It does not give it for a mouse that
is only moving: measured on this machine on 2026-09-12 by photographing the client, a cursor moved
across two rail rows produced no `WM_POINTERUPDATE` at all — the position stayed where the previous
press had put it, and the fill stayed on the row that had been tapped. So the one legacy mouse
message this class reads is the one that carries no click. It is not consumed, it takes no tap and
starts no drag, and nothing but the fill depends on it.

**At the lock, and in a finished match, every row is focus-only.** A row that opened a sheet at the
lock would be a sheet offering orders for a tick already resolving, which is the one thing screen 06
exists to prevent. Focusing is not an order and survives, exactly as `MAP` does on a digest card.

**The rail's help line changes**, because the old one stopped being true. *"Change it from the
digest"* was the whole answer while nothing on the rail did anything; now a BUILDS row reaches a
sheet where an order is changed. It reads *"What goes in when the clock hits zero. Tap a row to go
to what it is about."* — the same three lines at 8px, and it teaches the new affordance in the one
place a player is already looking for it.

## Consequences

- **The digest is still the only place an order is given.** Nothing added here queues, unqueues or
  answers anything; the build sheet a row opens is the same sheet the map opens.
- A tap on the rail can now change what is under a finger, which was not previously possible on that
  column. The destination picker a FLEETS row opens is the one the map's marker opens, and a parked
  fleet has no marker (`MapRender` draws a fleet only while it moves) — so this rail is the only
  place a standing fleet can be found without knowing where it is.
- **A hover is mouse-only and the screen does not need it.** Everything it decorates is reachable by
  a tap, and a touch device simply never draws one.
- The rail's rows are still not 44 pixels. ADR-052's open question — whether the digest and the
  locks rail should follow the sheet's row height — is untouched, and these rows are now targets at
  their current height, which makes that question sharper rather than answering it.
- `SIGNALS` rows are still read-only. A queued signal names nothing on the map, and the SIGNALS
  header is already the way back into the picker.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01 loses its "rows are not tappable" line and README.md item 5
  loses the same claim; `DESIGN-GUIDELINES.md` records the help line and that `HOVER_FILL` is drawn.
- **Code:** `LockstepClient/MainPage.{h,cpp}` (the `row` lambda, `SetPointer`),
  `NeuronClient/PointerInput.{h,cpp}` (`PointerPosition`), `Lockstep/Lockstep.cpp` (the match loop
  feeds the pointer in). Done, built and run.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `LocksRailTapTests` sweeps the rail on a state with a build queued and pins that a
row opens the build sheet for the system that build is on, and that the same row at the lock opens
nothing and focuses instead. Run on the client at `--tick 60`, with the pixel read back rather than
eyeballed: the ink under a hovered fleet row goes from `11,14,20` to `30,33,38`, which is
`HOVER_FILL` over the background, and returns to `11,14,20` when the pointer moves off it. That
measurement is what found the `WM_POINTERUPDATE` gap above — the first build drew the fill only
where a tap had last landed, and a screenshot is the only thing that could have shown it.

## Open questions

**Which end of a lane a proposal should focus.** The far end is the choice here and the near one is
defensible: the lane has two ends and the focus ring marks one. A map that could ring a LANE rather
than a system would not have to choose, and nothing draws that today.

**Whether a queued SIGNALS row should be a link back into the picker.** It is the one section whose
rows still do nothing, and the picker is one tap away at the header above them.
