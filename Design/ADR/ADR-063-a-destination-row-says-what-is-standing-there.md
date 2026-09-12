# ADR-063 - A destination row says what is standing there, and cannot yet say how it would go

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, in the 2026-09-12 build prompt: *"title `PELL`, second line `UNCLAIMED` / `YOURS` / `P3 - 11 +DEF`, right side `2 TICKS - ETA T9` and, on the line below it, the verdict in its colour."*
**Supersedes:** - (completes what ADR-052 opened)

---

## Context

ADR-052 gave the destination picker a 44-pixel row with four fields and said what they were for: the
old picker "listed `Faroe - ETA T1` and stopped", and "a player deciding where to move a fleet is
deciding between an expansion and a fight, and the screen was not telling them which was which."

What got built said whose the system is (`UNCLAIMED` / `YOURS` / `P3`, with `- CAPITAL` and
`- CONTESTED`) and what the lane costs (`2 TICKS - ETA T9`). Ownership is not the same question as
strength: a rival's system with nothing on it is an expansion and a rival's system with eleven ships
on it is a fight, and both rows read `P3`.

The digest already answers that question exactly once — for the destination a fleet is ALREADY
flying at, in the verdict box: `FLT1 ARRIVES T47 - YOU LOSE`, and under it `You arrive 14. P3 holds
11 +def. 6 of theirs remain, 0 of yours.` Those numbers come from `SnapshotFleet`, which carries
`previewMine`, `previewTheirs`, `previewMineAfter`, `previewTheirsAfter` and `previewDefended` — all
computed by `TickResolver::Preview` on the server, running the same `ResolveMelee` the battle runs.
`Snapshot.h` says why in as many words: the client "does not link `GameLogic` and must not learn a
rule to phrase a number."

## Options considered

### A. Leave the row as it is

Whose and how far, which is what it says today. Cheapest, and it leaves the picker answering the
easier half of the question it exists for.

### B. Say what is standing there

The row gains the hostile ships parked on the candidate and the fact they would fight as incumbents:
`P3 - 11 +DEF`. Every number in it is already in `MatchState` — a fleet parked on a system a player
can see is on the wire, because a fleet at a visible system is visible — and none of it is a rule.

### C. Say how the fight would go

`YOU WIN` / `HOLD` / `YOU LOSE` per candidate, in the verdict's colours, by the rule the verdict box
uses: won if the viewer has ships left and the defender none, held if both do, lost otherwise.

The rule is `ResolveMelee`: three rounds, half of effective strength per round, a 125% incumbent
multiplier, integer throughout (ADR-021). None of those three parameters is on the wire, and the
client cannot link `GameLogic` to get them. Computing this on the client means a second
implementation of the combat rule in a project that deliberately has none, which is exactly the
drift `Snapshot.h` forbids — and the snapshot's existing preview covers one destination, the one the
fleet is already flying at, which is the one the picker is not asking about.

## Decision

**B now, C when the wire carries it.**

**A destination row's second line says what is standing on the candidate**: `P3 - 11 +DEF`, the
summed ships of every hostile fleet parked there, with `+DEF` because anyone standing on a system
now is an incumbent by the time a fleet ordered this tick lands — which is the rule
`TickResolver::Preview` applies and the phrasing the verdict box's detail line already uses. The
garrison comes immediately after the owner, before `- CAPITAL` and `- CONTESTED`, because the
console's copy puts numbers first. A system with nothing hostile on it says nothing extra, so
`UNCLAIMED` and `YOURS` read as they did.

**The verdict is not drawn, and the client will not compute one.** The row shows what the player can
see, which is what the fog already tells them, and stops where the rules begin.

**What the wire owes:** a preview per candidate destination, for the fleet the picker is about.
`SnapshotFleet` carries one preview for `movingTo`; what a picker needs is the same five numbers
(`previewMine`, `previewTheirs`, `previewMineAfter`, `previewTheirsAfter`, `previewDefended`) for
each system one lane out of where the fleet is or is going. `TickResolver::Preview` already computes
exactly that from `(match, system, incoming side)` and is called once per fleet today; the change is
to call it per candidate and to give `SnapshotFleet` a list rather than a single set. The alternative
— putting `combatRounds`, `damagePercentPerRound` and `defenderBonusPercent` on the snapshot and
having the client apply them — is the one `Snapshot.h` rules out, and it is recorded here so it is
not re-proposed as the cheaper option: it is cheaper on the wire and it is a second implementation
of ADR-021.

## Consequences

- The picker now distinguishes an empty rival system from a defended one, which is the decision
  ADR-052 said the row existed to support.
- **It says what is there, not what would happen.** A player still does the arithmetic, and they
  cannot, because the parameters are the server's. That gap is real and this ADR does not close it.
- The line can now reach `P3 - 11 +DEF - CAPITAL - CONTESTED`, 34 characters at 8px in a sheet about
  608 wide, so it fits with room to spare.
- The garrison is what the player can SEE. A rival fleet arriving on the same tick is not in it, and
  neither is one parked at a system the fog has only remembered — which is the same limit the map
  and the digest are under, rather than a new one.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01's destination sheet gains the garrison and records the
  verdict as owed by the wire; README.md's list of what the design asks for and the tree does not
  have gains it.
- **Code:** `LockstepClient/MainPage.cpp`, the destination case of `DrawPanel`. The verdict half
  needs `GameLogic/Snapshot.{h,cpp}` and `Lockstep/SnapshotView.cpp` and has not been written.
- **AGENTS.md:** nothing.

## Verification

Run on the client at `--tick 60`, tick zero, `MOVE FLT 1`: rows for unclaimed neighbours read
`UNCLAIMED` as before, and the row for a neighbour with a rival fleet on it carries its strength.
Not unit-tested: the line is composed inside `DrawPanel` and is a drawn string, which is the line
AGENTS.md §2 draws — if it decides, it is tested; if it draws, it is a screenshot.

## Open questions

**Whether the picker should show the viewer's own garrison too.** `YOURS - 8` would say what a
reinforcement joins, and `YOURS` alone says nothing about it.

**Whether a per-candidate preview belongs on every fleet or only on the one being moved.** Every
fleet is simpler on the server and larger on the wire; only the moved one needs a request, which
this protocol does not have.
