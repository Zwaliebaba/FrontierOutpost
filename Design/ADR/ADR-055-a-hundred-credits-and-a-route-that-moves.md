# ADR-055 - A hundred credits, and a route that moves

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision: *"Lets start with 100 credits, 25 is really not enough to start with an interesting game"* and *"The animation of a fleet move is not nice. It should show a rolling dotted line in another color from the origin to the destination."*
**Supersedes:** - (re-pins the hash ADR-018 gates)

---

## Context

**The purse.** `startingCredits` was 20, and its comment said "enough for one building, so the
first lock is a real choice rather than a wait". A shipyard costs 20 and a mining station 15, so
what it actually bought was one building and no decision worth the name: build the thing you can
afford, or wait. ADR-053 put the price on every button, which made the shape of the problem
visible - a purse of 20 greys out the second row the moment the first is queued.

**The route.** A fleet under way was drawn as a marker at a fixed fraction of its lane - literally
`progress = 0.5F`, so a fleet on a three-tick lane was drawn at the midpoint on both of the ticks
it spent crossing. Nothing on the map said where it had come from or where it was going except a
label reading `FLT 1 - ETA T7`, and nothing moved. The one animation in the game was a countdown.

## Decision

**1. A match starts with 100 credits.** Enough for several buildings at once, so the first lock is
a plan - what to build, where, and what to keep back for a lane - rather than the single affordable
move. Nothing else in the rules changed; production, costs and the match length are as they were.

**2. A fleet under way draws its route, from origin to destination, in travelling dots.** The
owner's colour, a 2.5px dot on a 7.5px gap, walking toward the destination at 18 pixels a second.
Owner colour because everything a player owns wears it (ADR-027) and a rival's committed move is
the thing the fog rules mean you to read; the shape and the motion are what separate it from the
two other lines that can join the same two systems - a trade lane (solid blue) and a proposed one
(dashed blue, static). It is drawn on the plane with the lanes, under everything that stands on
them, because a route drawn over the systems would hide the two it is about.

**3. A fleet is drawn where its remaining ticks put it**, `(cost - left) / cost`, from two numbers
already on the wire.

**4. `DashedLine` takes an offset**, and that is the whole of the animation. The pattern slides
along the line; an offset of a whole period draws what an offset of zero draws, so a clock that has
been running for an hour is the same arithmetic as one that just started.

**5. The page is asked whether it needs a frame.** `MainPage::Animating()` is true while anything
is in transit, and the loop redraws on it. The idle throttle stands for every other board.

## Consequences

- **The pinned match hash moved**, which is ADR-018's gate doing its job: a rule changed, so the
  match did. Re-pinned to `0xC204BAED2104E2E7`, MSVC Debug and Release agreeing. The value before
  it had agreed across clang-on-Linux and both MSVC configurations, which is the evidence that the
  simulation does not depend on a toolchain; that cross-check was not repeated for the new value
  and is worth repeating the next time a Linux build is at hand.
- **Every stored match is unloadable**, per ADR-024. No cost in practice: a local game no longer
  keeps a store at all (ADR-054), and no `--serve` match is in flight.
- A map with a fleet under way now redraws continuously. That is the cost of the animation and it
  is bounded by the one condition that turns it on.
- The starting purse is a Phase 0 number like every other in `MatchRules`; 100 is a starting point
  for playtesting, not a measured one.

## Verification

`GameLogicTests`: the re-pinned hash, Debug and Release. `LockstepTests`: `FleetRouteTests` pins
the fraction a fleet is drawn at against its lane cost and remaining ticks, and pins that a still
board asks for no frames while a moving one does. The dots themselves are a screenshot - two frames
a second apart, the dots in different places along the same route.
