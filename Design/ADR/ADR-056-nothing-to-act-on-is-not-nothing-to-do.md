# ADR-056 - Nothing to act on is not nothing to do

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, on a screenshot of a tick-3 board: *"in the attached screen I am now in a situation where I cannot do anything anymore. How come?"* and *"can you remove the text 'LDR YOU 0', that does not add value."*
**Supersedes:** - (narrows ADR-034's "the digest is the order surface")

---

## Context

**The dead digest.** ADR-034 made the digest the order surface, and the orders rail says so in as
many words: *"What goes in when the clock hits zero. Change it from the digest."* Every control a
player has, other than the map and the SIGNALS header, is a button on a digest card, and those
buttons are composed per event: a proposal gets ACCEPT and DECLINE, a contact gets REDIRECT, an
economy event gets a BUILD - and only when there is something left to build.

So a tick can resolve, be reported, and carry no control at all. The owner's board was exactly
that: two systems, both already carrying a shipyard and a mining station, `BUILDS 0 AVAIL`, two
fleets standing, 66 credits in hand, and a digest of one card reading `PRODUCTION +17`. Nothing on
the card. The screen was telling a player with two fleets and a purse that there was nothing to do,
on the one surface it had told them to use.

The standing moves already existed - `OpeningActions` offers a build and a fleet move - but they
were reachable only through the branch that handles an EMPTY digest. A digest with one uneventful
entry in it took the other branch and got nothing.

**The leader line.** The top bar carried `LDR <name> <score>` unconditionally. When the leader is
the viewer it reads `LDR YOU 0`, beside a chip that already says `1ST / 6` and a score that is the
same number - three words restating what the two fields next to them already say. At tick zero it
is also the least informative thing on the screen: nobody has scored, and ADR-023's tiebreak falls
through to its last and explicitly arbitrary rule, the lower player id.

## Decision

**1. The standing moves are a floor under the whole digest, not the opening's special case.**
`OpeningActions` becomes `StandingMoves`, and `CardsOf` attaches them to the leading card whenever
no card offers a real control. FOCUS does not count as one: it moves the eye and gives no order, so
a card carrying only a MAP button is still a card nothing can be done with.

**2. The standing moves offer up to two standing fleets**, and never a fleet already under way -
`Match::Validate` refuses a redirect in transit, and a button whose order the lock is certain to
refuse is what ADR-053 took off this screen.

**3. The leader line names somebody else, or is not drawn.** "Public score, the leader is always
visible" is the anti-snowball and it is about knowing who is ahead of YOU; when that is you, the
bar already says so twice.

## Consequences

- A quiet tick now always carries at least one control while the orders are unlocked. A locked
  tick still carries none, which is the point of the lock.
- The digest can now show a MOVE button for a fleet that has nothing useful to do. That is honest:
  the alternative is a screen that offers nothing, and where to send a fleet is the question the
  game is made of.
- This does not add a sink for credits. A player holding two fully built systems has nothing to
  spend on but a trade lane, and expansion is the answer the rules intend - which is now reachable
  from the surface that claims to be the way to give an order.
- The top bar is one field shorter while the viewer leads, which gives the left-hand sentence more
  room rather than leaving a gap.

## Verification

`LockstepTests`: `NothingToActOnTests` builds the exact board from the screenshot - a production
event, no builds available, two fleets standing - and pins that the card offers an order, that a
fleet in transit is not offered a redirect, that a card which already offers something is left
alone, and that a locked tick offers nothing. `FleetRouteTests` pins that at tick zero the viewer
IS the leader, which is the state the bar must not spell out. Run on the client: the board was
reproduced by building out the capital, and the card carries `MOVE FLT 1`.
