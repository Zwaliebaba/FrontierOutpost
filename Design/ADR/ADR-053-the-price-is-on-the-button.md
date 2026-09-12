# ADR-053 — The price is on the button

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, on a screenshot of a practice match: *"after I moved a ship and press build, nothing happens anymore. When I gave a new command I got 'order refused, not enough credits' and now I have nothing to do anymore."*
**Supersedes:** —

---

## Context

A shipyard costs 20 credits and a mining station 15 (`MatchRules`). `Match::Validate` refuses a
build the purse cannot cover, against a running total, at the lock. That is correct and it stays.

What the player could see was this. The purse appeared in one place: inside the sentence on the
production card (`26 credits in hand`). The price appeared nowhere. The build button on the digest
card was drawn filled and identical before and after a tap, and the only sign a tap had done
anything was a `QUEUED` row in the rail on the other side of the screen. The button was a toggle, so
a second tap — the natural reaction to "nothing happened" — took the build back again. And when the
lock refused the build, the refusal arrived a tick later as `Order refused — not enough credits`,
naming neither the building nor the numbers, on a card whose own button re-offered the same build.

The log for that match shows twelve order edits reaching the server in one tick. Nothing was
broken on the wire. The player was doing everything right and being told nothing.

The client could not have said more, because it did not know more. `LockstepClient` never links
`GameLogic` (AGENTS.md §2), so `MatchRules` is invisible to it, and the snapshot did not carry
credits at all.

## Decision

1. **The snapshot carries the viewer's purse and the three prices** — `Credits()`,
   `ShipyardCost()`, `MiningStationCost()`, `TradeLaneCost()` — appended to the wire format. The
   purse is the viewer's own and nobody else's; another player's credits are a tell, and the
   snapshot's negative test still holds.

2. **The price is on every build control.** The digest button reads `SHIPYARD JANDAL 20 CR`, the
   build sheet's rows read `20 CR`, the BUILDS rail header reads `2 AVAIL - 26 CR`, a queued row
   reads `QUEUED -20`, a line under the queue says what is left at the lock, and the top bar carries
   the purse beside the score.

3. **The client refuses at the tap what the lock would refuse**, by the same rule: the running
   total of queued builds plus this one against the purse (`MatchState::CanAffordBuild`). A control
   for a build the purse cannot cover is drawn dim, says what is missing (`NEED 7 MORE`), and is not
   a target. `ToggleBuild` guards the same rule behind the controls.

4. **A queued build's button says so.** `SHIPYARD JANDAL 20 CR - QUEUED`, outlined in blue rather
   than filled, so the next tap is known to take it back.

5. **A refusal names the order and the numbers.** `Shipyard at Jandal - costs 20, you had 13`, with
   the system on the entry so the card focuses it; a fleet refusal names the fleet and destination;
   an offer names the recipient. The server composes the sentence, because the server is the only
   thing that knows why.

## Consequences

- The snapshot wire format grew by sixteen bytes at the end. The match store holds order sets, not
  snapshots (ADR-024, ADR-042), so no match in progress is affected; a client and server from
  either side of this change do not agree on a state message, and they never did across any other
  snapshot change.
- The client still learns nothing from `MatchRules`. It prices from the snapshot and applies one
  rule it can state in a sentence; the server remains the authority and still validates at the lock.
- A build refused at the lock for a reason the client could not see — the system changed hands
  during the tick — is still reported, now with its name.
- The trade-lane cost travels too but is not yet shown on the signal picker's `OPEN LANE` row; that
  row is a proposal, and pricing it is the next change, not this one.

## Verification

`GameLogicTests`: `ASnapshotCarriesThePurseAndThePrices`, the round trip, and
`ARefusedBuildIsNamedWithItsPriceAndThePurse`. `LockstepTests`: `BuildQueueTapTests` sweeps the
screen and asserts that a covered build can be queued from it, that an uncovered one cannot be
queued by any tap, and that no sequence of taps takes the queue past the purse.
