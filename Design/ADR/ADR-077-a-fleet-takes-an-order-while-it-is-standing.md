# ADR-077 - A fleet takes an order while it is standing, and the screen offers one only then

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Owner decision, on two screenshots of a practice match at T3 and T4: *"I can build things, but I cannot move a fleet anymore. How can I move a fleet, it does not seem to be possible now?"*
**Supersedes:** - (amends ADR-060's FLEETS row)

---

## Context

There are three surfaces on the main page that can open a fleet's destination picker, and on the
board in those screenshots none of them could.

**The digest's `MOVE FLT 1` is a standing move.** ADR-056 put the opening moves on the leading card
when a tick reports something but offers nothing to act on, and `DigestView::StandingMoves` is
reached from exactly two places: an empty digest, and a digest where no card carries an action that
is not `Focus`. A production line the player can build from is such an action. Measured on
2026-09-13 by walking `CardsOf` over `PlayedMatch(0..8)` with seat zero unbotted: at T0 the digest
offers one `MOVE` and one build; from T1 to T8 it offers a build and **no `MOVE` at all**, on every
one of those eight ticks, while the viewer's single fleet stands at its capital the whole time.

**The map draws no marker for a parked fleet.** `MapRender` collects a fleet into its drawables only
when `order == Move && from != to`, so a fleet standing at a system is drawn as part of that system
and there is nothing to tap. That is the right picture -- a fleet at rest is a garrison, and ADR-059
is about keeping the markers that do exist clear of the systems at either end -- but it means the
map contributes no entry point.

**The locks rail names the fleet and goes nowhere useful.** ADR-060 made a rail row a link to what
it is about and chose, for FLEETS, that a fleet under way opens its picker and one standing still
focuses where it stands. Its own Consequences section names the hole this leaves: *"a parked fleet
has no marker (`MapRender` draws a fleet only while it moves) -- so this rail is the only place a
standing fleet can be found without knowing where it is."* It found the place and stopped there.

So from the second tick of any match onwards, the only fleet order a player can give is the one they
give before the first lock, which is what the owner hit.

Underneath that sits a second defect the first one was hiding. `OrdersOf` emits a `FleetOrder` for
every fleet of the viewer's whose stance is `Move`, and `ViewOf` sets that stance from the
snapshot's `ticksRemaining` -- so a fleet the server already has on a lane is `Move` on every tick it
flies, and every order set sent while it flies carries a fresh order for it. `Match::Validate`
refuses each one as `FleetInTransit`, and `TickResolver` turns a refusal into a digest entry. Measured
the same day, six bots on seed `0x5349474E414C5321`: at ticks 5 through 11 where the viewer had
fleets in transit, **every fleet order the client produced was refused** -- 1, 2, 1, 1, 4, 4 and 1
orders per tick, and the same counts refused. A player who taps anything at all while a fleet is
flying is told the next tick that an order they never gave was refused.

## Options considered

### A. Offer the standing moves on every digest card

Reach `StandingMoves` unconditionally rather than only when nothing else can be acted on. It is one
condition removed and it restores a `MOVE` button to the screen. What it costs is the digest: the
card column is 400px wide, buttons that do not fit are dropped rather than wrapped, and a card
already carrying `BUILD`, `ACCEPT` and `DECLINE` would push its own controls off the rail to make
room for a fleet control that has nothing to do with what the card reports. ADR-056's rule is also
a good one -- the standing moves are a floor under an empty screen, not a second copy of every
control -- and this reading deletes it.

### B. Draw a marker for every parked fleet on the map

Give a standing fleet something to tap where the player is already looking. It is the most
discoverable answer and it is the most expensive: a marker per fleet per system, stacking where two
fleets hold one place, competing with the system's own label and ring, on a map whose whole
occlusion model is painter's order. It also makes the map say something it deliberately does not --
a garrison is part of what a system is, and ADR-063 already puts the hostile ships standing at a
destination on the destination row rather than on the map.

### C. The FLEETS row opens the picker, for a standing fleet as well as a flying one

The row is already a link and already opens this exact sheet; what it links to just depends on the
fleet's stance. It costs nothing on any other surface, it needs no new control anywhere, and it is
what ADR-060's option C says a row does -- *"it focuses a system, or opens the sheet that is already
the way to change the thing the row is about."* What it costs is discoverability: the row looks the
same as it did, so a player who has not learned that rail rows are links has learned nothing new
here. The rail's help line -- *"Tap a row to go to what it is about"* -- is where that is taught and
it is already on the screen.

## Decision

**C, and its mirror image.** A FLEETS row opens the fleet's destination picker when the fleet is
standing, and a fleet the server already has on a lane opens nothing anywhere and focuses where it
is going instead. The picker is rooted at where the fleet stands, every surface asks the same
question before offering a control, and the order set carries a fleet order only for a move the
player gave this tick.

**`Fleet::underWay` is the fact all of that is asked of, and it is why the field exists.** `order`
cannot answer it: a move ordered this tick sets the stance to `Move` exactly as a snapshot of a
fleet already flying does, and the two take opposite treatment everywhere it matters -- the ordered
one is an order to send, a picker to re-open and a `MOVE` the digest may offer; the flying one is
none of those. `ViewOf` sets `underWay` from the snapshot's `ticksRemaining` and nothing else ever
writes it, so an edit cannot make a fleet look flown.

**The client refuses at the tap what the lock would refuse**, which is ADR-053's rule applied to the
one order surface that still had a control the lock was certain to refuse. `Action::OpenFleet`
carries the same guard behind the controls that `Action::ToggleBuild` does, so the rule lives in one
place rather than in three that agree.

**A move ordered this tick is still redirectable.** `Match::Validate` refuses a second order only
once the server has the fleet on a lane, so before the lock the picker re-opens on its own fleet and
picking again replaces the order rather than adding a hop to it -- an order is an edit until the
lock (ADR-031), and the digest's `MOVE` offers the fleet again for the same reason. A picker left
open across a lock that sent its fleet away closes, because what it was about is no longer something
to order (ADR-065).

## Consequences

- **A parked fleet is reachable from exactly one place, and it is a column of text.** That is the
  cost of C over B and it is real: nothing on the rail row looks more like a control than it did
  yesterday. The row is a link like the BUILDS rows above it and the help line says rows are links.
- **`MatchState::Fleet` grew a field that duplicates nothing on the wire.** It is derived from
  `ticksRemaining`, which the snapshot already carries; what it buys is that the derivation happens
  once, in `ViewOf`, rather than being re-guessed as `order == Move && from != to` in four places --
  which is what `DigestView` was doing, and what made a move ordered this tick indistinguishable
  from one already flying.
- **The refusal spam stops, and with it a class of digest entry the player could not act on.** An
  `Order refused` that names an order nobody gave is worse than no entry: it teaches a player that
  the screen's account of their own orders cannot be trusted.
- **The digest still offers `MOVE` only when there is nothing else to act on.** ADR-056 stands; what
  changes is that it is no longer the only way to move a fleet, so it can go on being a floor.
- A fleet under way now focuses its destination from the rail AND from its own marker on the map,
  where the marker used to open a picker. Two surfaces that disagreed about one fleet now do not.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01 -- the FLEETS row's line under "Rows are links", and the
  Destination sheet's "opened by" list. Done in this commit.
- **Code:** `LockstepClient/MatchState.h` (`Fleet::underWay`), `LockstepClient/MainPage.cpp` (the
  FLEETS row, the map's fleet hit, `Action::OpenFleet`, `Action::ChooseDestination`, `ReopenPanel`,
  the Destination sheet's origin), `LockstepClient/DigestView.cpp` (`StandingMoves`),
  `Lockstep/SnapshotView.cpp` (`ViewOf` sets it, `OrdersOf` reads it). Done, built and run.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `FleetMoveTapTests` sweeps the whole screen on a T4 board whose digest carries a
build and no `MOVE`, and pins that some pair of taps opens a picker for the standing fleet and
orders it somewhere that reaches `OrdersOf`; that no tap anywhere gives a fleet already on a lane an
order or a redirect; and that the order set a board with a fleet in transit produces is refused by
`Match::Validate` for nothing. `NothingToActOnTests` keeps the flying fleet out of the standing
moves and adds the other half -- a move ordered this tick is still offered.

Run on 2026-09-13 against the real client, a practice match at `--tick 20` driven by real taps
(`SendInput`, the only input the Windows Pointer API path believes) and photographed with the client
area cropped the way `Build/Screenshot.ps1` crops it. At T2, with the digest carrying
`SHIPYARD L1 DOTHAN 20 CR` and no `MOVE`: the FLEETS row opened `MOVE FLT 1 - PICK LANE` listing the
four lanes out of Dothan, with the focus ring on Dothan; picking `JANDAL` put `FLT 1 10 -> JANDAL`
/ `T3` on the rail and the marker on the lane, and T3 reported `CLAIMED JANDAL`. Repeated over the
two-tick lane to Xander so that the fleet spent a tick visibly in transit: two further taps were made
during that tick, and the T4 digest carried four events and **no `Order refused` among them**.

## Open questions

**Whether the map should carry a badge for every parked fleet after all.** Option B above is item
1.1 of a UX review written the same day -- `Design/UIImprovement.md`, untracked in the working tree
on 2026-09-13 and so not a plan under `Design/README.md` §2 -- which proposes a ship-count badge
beside every system holding a fleet, a picker sheet where a system holds several, and a
`Move FLT n` row appended to that system's build sheet. This ADR takes the cheapest thing that makes
the order reachable and does not foreclose any of that: a badge would be a second way to the same
picker, and the guard on `Action::OpenFleet` is what a badge would have to respect. **The owner
should decide whether 1.1 still runs**; if it does, its ADR supersedes this one's answer to "where is
a parked fleet found" and leaves the rest standing.

**Whether a FLEETS row should say that it leads somewhere.** The BUILDS rows have the same problem
and have had it since ADR-060; `HOVER_FILL` answers it for a mouse and answers nothing for a finger.
A chevron on the rows that are targets is the obvious shape and it is a change to every section of
the rail, not to this one.

**Whether the digest should offer `MOVE` for a fleet that is standing on a contested border.** The
standing moves are ranked by nothing today -- the first two fleets in the list win. A fleet with a
rival in reach is a better button than a fleet in the rear, and the preview that would decide it is
the same one ADR-063 wants for the destination rows.
