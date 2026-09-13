# ADR-079 - A fleet standing at a system is drawn there

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Owner decision, choosing "rescope 1.1, fix 2.3, then build" on the conflict between `Design/Plans/UI-01-ClientImprovements.md` item 1.1 and ADR-077.
**Supersedes:** - (amends ADR-055's account of what the map draws)

---

## Context

`MapRender` collects a fleet into its drawables only when it is on a lane, so a fleet standing at a
system was drawn by nothing at all. On `Design/UI/screens/01-main-page.png` — tick 11 of a six-seat
match — the locks rail lists ten fleets, every one of them `HOLD`, and the map carries not one mark
for any of them. Where the player's strength is, and how strong a system they are considering flying
into is, could only be read by scanning a column of ten near-identical rows.

That was always a cost. ADR-077 sharpened it: a FLEETS row is now the way a parked fleet is ordered,
so the same column is carrying both jobs — it is the only place a fleet can be found AND the only
place it can be ordered from — while the surface the player is actually looking at says nothing.

**The hostile half already exists elsewhere and is the same fact.** ADR-063 puts the ships standing
at a candidate destination on that destination's row in the picker, with `+DEF`, because a lane is a
fight or an expansion by what is standing at the far end. That is the number this badge draws, in
the place the decision is actually being made.

UI-01 item 1.1 proposed badges, and this ADR is that item built. What it does NOT do is what 1.1's
first draft also proposed: fix the entry-point problem. ADR-077 did that, and the owner's decision
of 2026-09-13 rescoped 1.1 to visibility before a line of this was written.

## Options considered

### A. Leave the map to systems and lanes, and let the rail carry it

What the tree did. The rail is complete and correct — every fleet, its strength, where it stands —
and it costs nothing to keep. What it costs is that the map, which is the surface a player looks at
to decide anything, cannot answer "where am I strong" or "what is sitting on that border" at all.
Ten rows of `FLT 13 10 HOLD HOLLIS` is a list, not a picture, and the whole point of drawing a
galaxy is that a picture answers those two questions at a glance.

### B. A marker per fleet, like the one a fleet in transit gets

Consistent with what a moving fleet already gets, and it needs no new shape. Three of the ten fleets
on the reference board stand at Halvorsen and three at Hollis, so this stacks three markers on one
node — each with a stem, an arrowhead and a label — on a map whose entire occlusion model is
painter's order. It also says the wrong thing: three fleets at one system is one garrison and one
decision, not three things.

### C. One badge per owner per system, carrying total ships

A chip beside the name. It is one shape per owner per system however many fleets make it up, it
reads as a strength rather than as a list, and it is the same number ADR-063 already puts on a
destination row. The cost is that a badge is not a fleet: tapping one when several fleets stand
there cannot open a picker, because a picker is about one fleet — so it needs a sheet in between.

## Decision

**C.** Every owner with fleets standing at a system gets one badge beside that system's name,
carrying their total ships there. **Yours is filled in your own blue with the number knocked out in
the background colour, and it is a target; a rival's is their colour at 0.35 with the number at full
strength, and it focuses the system exactly as the disc does.** There is no order to give about
somebody else's ships, and a control that looks like one is the defect ADR-053 spent itself removing.

**Tapping your own badge opens the picker, through a fleet list when there is a choice to make.**
One fleet standing there goes straight to `MOVE FLT n - PICK LANE`, because a sheet of one row is a
tap spent on a question with one answer. Several open `FLEETS AT DOTHAN - PICK ONE` first, whose
rows are fleets and whose taps are `Action::OpenFleet` — so **both routes end at the same guarded
action**, and ADR-077's refusal to offer a picker for a fleet already on a lane covers the map
without being written twice.

**`Fleet::OnALane` is the rule that a fleet is a marker or a badge and never both**, and it is on
`Fleet` because it had been written out four times — three in `MapRender` and once in
`MainPage::Animating` — before it had a name. It is not `underWay`: a move ordered this tick is on a
lane at progress zero from the moment it is given (ADR-055) and is not under way until the lock.

**A badge is focus-only at the lock**, which is what UI-01 asked for and what a locks-rail row does
(ADR-060). It is the one control on the map that follows the rail rather than the disc beside it.

**The badge is square and the legend says `SHIPS HOLDING`.** There is no rounded-rectangle primitive
in `ShapeRenderer` and every other chip on this screen is square, so adding one for this is not
warranted. UI-01 asked the legend to read `n FLEET HOLDING`; the number on the badge is ships — a
system holding three fleets of three wears one badge reading 9 — and a legend naming it "fleets"
would teach the wrong reading of the only number the map now carries.

## Consequences

- **The map reveals nothing the client was not already told.** `GarrisonsAt` reads
  `MatchState::fleets`, which is what the snapshot sent through the fog: a rival fleet the viewer
  cannot see is not in that list and cannot be badged (ADR-022). What changes is that a fact the
  client already had stops being invisible.
- **A system now has two targets where it had one**, a disc and a badge, doing different things.
  That is the point — the disc is the system and the badge is what is standing on it — and it is
  paid for in hit-list order: the badge is pushed after the disc so that it wins the overlap.
- **A new sheet kind exists whose subject is a system rather than a fleet.** `Panel::FleetList`
  restores like the build sheet does, by system id, and closes when the garrison has left.
- Two badges on one system is possible — a hold-fire pact leaves two owners' fleets standing in one
  place — and they step right in a stable order, viewer first. Nothing caps the number, because
  nothing bounds it usefully and the realistic count is one.
- It does not answer where a fleet's strength came from or where it is going next; a badge is a
  number and the rail is still the list.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` §01 Map and the sheet list, `DESIGN-GUIDELINES.md` §Map,
  `README.md` §Photographing (both notes that said a parked fleet has no marker, plus what to do on
  a locked desktop). `Design/Plans/UI-01-ClientImprovements.md` item 1.1 is this. Done in this commit.
- **Code:** `LockstepClient/MatchState.h` (`Fleet::OnALane`), `LockstepClient/MapRender.{h,cpp}`
  (`MapHit::fleetsAt`, `Garrison`, `GarrisonsAt`, the badge, the legend entry, and the three copies
  of the lane predicate folded onto one), `LockstepClient/MainPage.{h,cpp}`
  (`Action::OpenFleetsAt`, `Panel::FleetList`, `StandingFleetsAt`, the sheet, `ReopenPanel`).
  Done, built and photographed.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `GarrisonBadgeTapTests` sweeps the MAP PANE ONLY — the rail reaches the same picker
and would answer every one of these for the wrong reason — and pins that a badge opens the picker
for the fleet standing under it on a board with nothing in transit; that a system holding two of
yours opens the fleet list and that a row of it opens the picker; that a board where every fleet
belongs to a rival offers no picker anywhere on the screen; that a badge opens nothing at the lock
and focuses instead; and that a fleet the server has on a lane always reports `OnALane`. The first
two were run against a build with `GarrisonsAt` returning nothing and both failed, which is what
says they are testing the badge rather than the rail.

Photographed on 2026-09-13 against a `--serve --phase0 --tick 20 --bots 5` server with a `--join`
client, at T16: `DOTHAN 10` filled blue beside the capital — the same 10 the rail's
`FLT 1 10 HOLD DOTHAN` carries — with `Xander 16` and `Pell 24` washed in their holders' colours,
and `SHIPS HOLDING` in the legend. **The desktop was locked, so this was driven by a posted
`VK_RETURN` rather than by taps**; the sheets a badge opens were exercised by the tap tests above
and not photographed.

## Open questions

**Whether the map's disc should also be focus-only at the lock.** A badge is, and a locks-rail row
is (ADR-060), but `Action::OpenSystem` still opens an inert build sheet at the lock because ADR-065
says a sheet at the lock stays and says why. So at the lock a disc opens something and the badge
beside it does not. Both rules are defensible and they now meet on one node; this ADR followed
UI-01's instruction for the badge and left the disc alone rather than deciding the pair.

**`01-main-page.png` has not been retaken.** UI-01 1.1 asks for badges on Halvorsen, Hollis, Ulme,
Brannoc and Nyx, which needs a board the viewer has actually played — and playing one needs taps.
The capture above is the feature on a board the viewer did not play, and the reference capture is
still from before this change.
