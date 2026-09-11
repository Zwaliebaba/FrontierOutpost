# ADR-022 — Fog remembers: a system once seen stays known, stamped with when

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Build session for `Design/Plans/4X-01-CoreLoop.md`, Step 8. §3 names this ADR and gives a recommendation, which is taken here with the reasoning written out.
**Supersedes:** —

---

## Context

The client already assumes fog. `Design/Screens/README.md` shows a map of eleven systems under a
header reading **"41 SYSTEMS"**, and `MatchState.h` says so in as many words: *"41 systems on a map
showing 11 is not an inconsistency, it is fog."* Nothing implemented it. Until Step 8 every player
could see the whole board, because there was no snapshot and no server to filter one.

The one-pager constrains the answer in four places rather than one:

- **"Ships can defend or push, never both; lanes make every allocation a visible commitment that
  others can read and exploit."** Reading a rival's allocation is a *skill the game is about*, so
  fleet movement has to be visible — after it commits.
- **"Fleets in transit are public once departed."** The asymmetry is the point: blind at the moment
  of choice, visible afterwards.
- **"Shared scouting pays visibly, as fog lifting on the map."** So sharing has to produce a change
  a player can see immediately, not a subtle statistical advantage.
- **"Early: how to read the map — which lanes matter, which systems are worth the reach."** A player
  learns the map. A fog that unlearns it is working against the thing the design says players are
  meant to be getting better at.

And one constraint from this tree: the snapshot is what gets **sent** (Step 8, `4X-02`). There is no
second filter downstream, so whatever visibility decides is what a player can read off the wire.

## Options considered

### A. No fog: everybody sees everything

Simplest, and it is what Steps 0–7 did by omission. It deletes scouting as an activity and makes
shared scouting — one of the one-pager's three proposal kinds — a button that does nothing. It also
removes the reason to move a fleet anywhere except toward a fight.

### B. Live fog: you see what you can see right now, and nothing else

A system out of range goes dark and is removed from the player's map entirely. Classic, and cheap:
no per-player memory, so no state to store, hash or send.

It fails the *learning* constraint. A player who scouted the frontier on day two and came home
would arrive on day three with no record of what they found, and would have to send the same fleet
back over the same lanes to re-learn it — four times a day, for three weeks. Worse, it makes the
map *flicker*: a system appears and vanishes as a fleet passes, which reads as a bug rather than as
a rule.

### C. Remembered fog: once seen, a system stays known at its last-seen state, with a tick stamp

The player keeps what they learned. A system out of range still appears on the map, greyed, showing
who held it **when it was last seen** and labelled with that tick. The digest can then say "as of
T41" and mean it.

It costs per-player state — one small record per player per system, which at twelve players and
sixty-one systems is a few hundred entries — and that state has to be in the hash, because a
snapshot is built from it and two machines that disagreed about it would show two different maps of
the same match.

### D. Remembered fog with decay: knowledge expires after N ticks

Adds a parameter and a rule that the player has to learn in order to predict their own map. The
tick stamp already tells them how stale a fact is, and letting them judge that is strictly more
information than deciding it for them. Rejected as a knob that buys nothing the stamp does not.

## Decision

**C. Remembered fog, with the visibility rule below.**

A player sees, live, each tick:

1. Every system they hold.
2. Every system one lane from a system they hold.
3. Every system a fleet of theirs is at, or one lane from it. A fleet under way sees from both ends
   of the lane it is on — it is somewhere between them and there is no third thing to be next to.
4. Everything a shared-scouting partner sees, applied as a **union** after everything else, so an
   agreement can only ever add.

The range is `MatchRules::scoutingRangeLanes`, initially **one**. It is lanes, not ticks: a lane
costs two to four ticks on the frontier, and a rule measured in ticks would make the frontier's
long lanes invisible from either end.

**A system that has ever been live stays `known`**, carrying the owner, the buildings and the tick
it was last seen. A system that has never been seen is **absent from the snapshot entirely**, not
present and blank — a blank entry still tells the player that something is there and where it is.

**Live systems report now; remembered systems report then.** Siege progress and capture stamps are
reported only for a live system, because a remembered siege may have ended three ticks ago and
reporting it as current would be the snapshot lying rather than being stale.

**Fleets follow a different rule, and it is the one-pager's:** a fleet in transit is public to
everybody from the moment it departs. A fleet parked at a system is visible only to players who can
see that system *live* — not merely `known`, because a remembered system must not report a garrison
that has since marched away.

**Standings are never fogged.** Score, placement and custodian status are public for every player,
because "public score, the leader is always visible" is the anti-snowball and it does not work in
secret.

**Visibility is computed in the resolver and stored on the match, not computed in the snapshot.**
What a player has seen is state: it is in the hash, it is reproducible, and a replay of tick 41
builds the same map the player saw at tick 41.

## Consequences

**The snapshot is the security boundary**, and `Snapshot.cpp` says so at the top. Every field added
there gets sent. The test that matters is the negative one — a system three lanes away, a garrison
parked out of sight, an offer between two other empires, and no lane running into the dark, because
a lane with one visible end would itself say something is there.

**Shared scouting is now worth proposing.** Accepting it visibly doubles a player's map, and
cancelling it takes the live view away while leaving everything already learned. That asymmetry is
deliberate: you cannot un-tell somebody what they saw.

**The map gets noisier over a long match.** By the end, most players know most of the board at
varying staleness. Whether that makes the late game legible or cluttered is a Phase 0 question, and
the answer, if it is cluttered, is a client-side filter rather than forgetting.

**Per-player memory is `O(players × systems)`** and is hashed every tick. At twelve players that is
732 records. If a profile ever objects, the fix is a tighter record, not dropping it from the hash.

## What this changes elsewhere

`Match` grows `SeenSystem` per player per system and `Snapshot` is new. `MatchState.h` on the client
already has the shape this fills; wiring the two together is where the executable's composition root
earns its keep, since the client never links `GameLogic` (AGENTS.md §2).

`MatchState::Owner` still has three players and a neutral, and a snapshot now carries standings for
six to twelve. Widening it is the remaining half of Step 8's client work and is a design question
about the token palette rather than a mechanical one.

## Open questions

**Whether a capital should be visible to everybody from tick one.** The one-pager's "dense start"
means neighbours meet on day one anyway, and first contact already fires on adjacency. But a player
cannot currently *plan* toward a rival they have not met, which may make the early game feel
blinder than intended.

**Whether shared scouting should be cancellable at all, and how.** It is recorded as an agreement
with no expiry and nothing removes it today except a test reaching in. The one-pager gives
cancellation to trade lanes explicitly and says nothing about the other two.

**What a player sees of the sealed region before it opens.** It is on the map from tick one by
design — "visible from tick one, opens at a known tick" — but the region anchor is a system like any
other here, so it is currently fogged like any other. That is probably wrong and is Phase 2's to
settle.
