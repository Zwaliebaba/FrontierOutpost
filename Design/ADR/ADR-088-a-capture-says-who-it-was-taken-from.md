# ADR-088 - A capture says who it was taken from

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision, answering ADR-082's open question: *"Put capturedFrom on the wire."*
**Supersedes:** - (closes ADR-082's open question)

---

## Context

ADR-082 made the map's `CAPTURED Tn` label age out after three ticks and drew a capture the viewer
GAINED in their own colour rather than in red. It could not finish the job: `SnapshotSystem` carried
`capturedAt` and no previous owner, so *"a rival took this from me"* and *"a rival took it from
another rival"* were the same fact to the client, and both were drawn in red.

Red on this screen means the viewer's loss -- it is the battle card's ink, the concede row's and the
`YOU LOSE` verdict's -- so a board where two rivals are fighting each other reads, to a third party
watching from across the galaxy, as their own defeat.

ADR-082 put two answers to the owner and recommended the wire.

## Options considered

### A. Remember owners in the session

The client keeps the previous snapshot's owners and calls a system lost when it was the viewer's in
the last state it drew and is not now. Free: no wire change, no re-pin. It is wrong for exactly the
player most likely to be looking at a board full of captures -- one who joins mid-match or
reconnects after a drop, whose first snapshot has no predecessor to compare against (ADR-044 sends
the whole backlog on arrival, so the digest is complete and the map's memory is empty).

### B. Put it on the wire

`SnapshotSystem` gains a `capturedFrom` PlayerId, written by the resolver at the moment the system
changes hands. Always correct, for every client in every state.

## Decision

**B**, on the owner's instruction. `SystemState::capturedFrom` is set at both capture sites in
`TickResolver` -- from `before.owner`, which is invalid when the system was unowned -- and travels
with the snapshot.

**Three cases, three inks.** A capture the viewer gained is drawn in their own colour; one taken
from them is red; one between two rivals is the new owner's colour at 0.7, which is what UI-01 1.4
asked for. The third case is the one that did not exist before this ADR.

**It is not a leak.** A capture is already reported to every player who can see the system, and the
digest entry that reported it names both sides. What the map lacked was a way to ask the same
question a tick later, about a fact it was already told.

**It is absorbed into the match hash**, like `capturedAt` beside it: it is authoritative state, and
state that exists is state a determinism check should cover.

## Consequences

- **The snapshot wire format grew by four bytes per system.** The match store holds order sets and
  replays them (ADR-024, ADR-042), so no stored match is affected; a client and a server from either
  side of this change do not agree on a state message, and they never did across any other snapshot
  change (ADR-053 made the same note).
- **`TheWholeMatchHashIsPinnedAcrossToolchains` was re-pinned**, and this is the second kind of
  re-pin rather than the first. **No rule changed**: the same orders replay to the same match, no bot
  plays differently, and only the fingerprint's input set grew. The test's comment now distinguishes
  the two, because the expensive kind invalidates every stored match and this one does not.
- **The bot match never exercises the player-to-player case.** Eighty-four ticks of `ExpandNear`
  claim open space and take nothing off anybody -- the same finding `BalanceProbeTests` records as
  115 sieges and zero captures -- so the scripted match pins the open-space half and a driven siege
  in `MatchTests` pins the other. A test that asserted both against the bot match would have
  asserted a balance property by accident.
- The client now holds a fact it does nothing else with. That is fine and worth naming: it is one
  field, and the alternative was a client-side memory that is wrong on arrival.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Map (the third ink), `SCREENS.md` 01. ADR-082's open question
  is answered by this file rather than edited into it. Done in this commit.
- **Code:** `GameLogic/Match.h` (`SystemState::capturedFrom`), `GameLogic/TickResolver.cpp` (both
  capture sites), `GameLogic/Match.cpp` (the hash), `GameLogic/Snapshot.{h,cpp}` (the record and the
  archive), `Lockstep/SnapshotView.cpp`, `LockstepClient/MatchState.h` (`SystemNode::capturedFrom`),
  `LockstepClient/MapRender.cpp` (three inks). Done and built.
- **AGENTS.md:** nothing.

## Verification

`GameLogicTests`: `CaptureTakesTwoConsecutiveUncontestedTicks` -- a driven siege -- now asserts that
the captured system records player 1 as the loser and a non-zero tick.
`AClaimOutOfOpenSpaceRecordsNoLoser` walks an 84-tick bot match and pins that every capture either
names a loser who is not the current owner or names nobody and has an owner.
`ASnapshotSurvivesTheRoundTrip` carries `capturedAt` and `capturedFrom` across the wire.
`LockstepTests`: `CaptureInkTests` pins the three cases on the view model. All 571 methods across the
five suites pass.

**Not photographed** -- captures are being taken as one pass at the end of UI-01.

## Open questions

**Whether the digest should use it too.** A `SystemLost` entry already names who took it, because the
resolver wrote the sentence; nothing there needs this field. If the verdict box is ever built
(ADR-063), it might.
