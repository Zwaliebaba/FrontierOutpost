# ADR-039 — The client can say things, and the lock shows

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, "do both" on outgoing signals and the at-lock screen, 2026-09-12.

**Implements:** SCREENS.md 06 (at lock) and the `SIGNALS` section of 01, and closes the four
undrawable actions PROMPT.md's Fixture note listed — REBUILD LANE, PLAN ROUTE, WITHDRAW, HOLD FIRE.

---

## Context

`OrdersOf` could build three things: fleet moves, builds, and one answer to a proposal. `OrderSet`
has seven fields. The four it could not build were `proposals`, `withdrawals`, `cancellations` and
`concede`.

In play that meant **diplomacy was receive-only**. A player could accept or decline what was sent to
them and could never open a lane, take an offer back, close a lane they were on, or hand over an
empire they had lost interest in. `GameLogic` has implemented all four for as long as it has
existed, and the tests cover them. The gap was entirely in the client.

The sharpest way to put it: the `Diplomat` bot policy sends a proposal every tick. Since ADR-037 a
host can put that bot in a seat. **A machine in a seat could do something a person at a keyboard
could not.**

The `SIGNALS` rail said `- none sent -` and always would, because `MatchState` had no model of an
offer leaving. The comment above it said so, which was honest and had been honest for a while.

Separately, SCREENS.md 06 asks that at countdown zero the screen say so: the rail flips to a filled
`LOCKED`, the digest header reads `T47 PENDING`, a notice says what is happening, and every control
goes inert. Inert and dim were already true. The saying was not.

## Decision

**`SignalRow` is a thing the client offers, composed from the snapshot, exactly as a build row is.**
The server has no opinion about what an offer should be called, and everything needed to find one is
already on the wire — no protocol change:

- a lane carries `tradeLane`, so a lane this player is on can be closed;
- a proposal is sent to **both** parties, so this player's own offers are here to be withdrawn;
- a system carries its owner, so a border is one lane with two owners.

Six kinds: `OpenLane`, `ShareScouting`, `HoldFire`, `Withdraw`, `CancelLane`, `Concede`. A row knows
which order it becomes and carries the one or two fields that order needs — a lane id, a proposal id
— because **a row has to be able to become an order**, and a screen position cannot.

**A queued signal is an order like any other**: local until the lock, sent with the fleet moves and
the builds, taken back by tapping it again. That is decision three of the one-pager applied to the
one column that did not have it.

**Conceding takes two taps on the same row**, and the row itself says so — `TAP AGAIN TO CONFIRM`.
It is the only order on this screen that cannot be undone once it resolves, so it gets a warning;
it is not a modal dialog, because everything else about it is an ordinary edit. The arming lives on
the screen rather than in the order, because it is about fingers and not about what is sent. Any
other tap disarms it, and `Concede` is always the last row and never trimmed away, because a row
that moves between ticks is a row somebody double-taps by accident.

**Offers are only to empires this player has actually met** — met meaning "owns a system this player
can see now", the same condition first contact is reported on. Offering to share maps with an empire
nobody has found would be offering a map of somewhere neither has been.

**Rows are built in snapshot order**, lanes then players, both already sorted by id, so the list
does not reshuffle under a finger between one tick and the next. The panel shows fourteen and counts
the rest, the same bargain `availableBuilds` already makes.

**The `SIGNALS` header is a control.** It is the only section header on that rail that is, and it is
the way into the picker — there is no map object to hang `Concede` or `ShareScouting` on.

### Screen 06

`T47 LOCKED` in the top bar, the countdown in grey rather than amber, `T47 PENDING` in the digest
header, a filled grey `LOCKED` chip on the rail, an amber notice — *"Resolving T47. Controls return
with the new digest. Anything you tap now is an order for T48."* — and `LOCKED TOGETHER · T47
RESOLVING` in the footer.

Amber is the deadline colour; at zero there is no deadline left to warn about, and a countdown that
stayed amber on `00:00:00` read as *hurry* to somebody who could no longer do anything.

## The bug underneath screen 06

**`FormatCountdown` truncated, so `00:00:00` was on the screen for the whole of the last second** —
while the rail still said `UNLOCKED` and still took edits. The screen announced that the deadline
had passed and then went on accepting orders, which is precisely the confusion screen 06 exists to
remove. It now rounds up: `00:00:01` means there is still a second of it, `00:00:00` means there is
not, and the clock reading zero is the same event as the rail reading `LOCKED`.

This was found by photographing the running client, not by reading the code.

## Consequences

**Screen 06 is a sub-second screen when the server is on time**, and that is correct. The client
locks when its countdown reaches zero; the server resolves at the same instant and pushes the new
state within one poll. The screen is what you see when the server is **late** — which is exactly
when a player needs to be told that their taps are now for the next tick.

**`Proposal` rows in the rail are still only the ones awaiting this player's answer.** An offer this
player made does not appear there; it appears as a `Withdraw` row, which is the only thing they can
still do about it.

**`HoldFire` is fixed at three ticks.** It is the one-pager's own example, and a promise nothing
enforces is not improved by making its length adjustable.

## What was verified, and what was not

The workstation was locked again, so **no synthetic tap can land**. Two things were done instead.

**A scratch harness, compiled outside the repo against the same libraries and the same two `.cpp`
files**, built a real six-seat match, ran fourteen ticks of bots, took a real snapshot, ran it
through `ViewOf`, queued signals, ran `OrdersOf`, and pushed the result back through
`MatchSimulation`'s byte seam. It confirmed end to end: a border offers a lane; a met empire offers
scouting; two queued offers become two `ProposalOrder`s; the server accepts them and logs
`proposal-sent to=1 from=0` twice; a sent offer comes back as a `Withdraw` row; withdrawing it is
accepted; and conceding produces `custodian player=0` in the instrumentation log. Nothing was
rejected as malformed at any point.

**The running client was photographed** for the rail (`SIGNALS · 9 TO SEND >`), for screen 06 in
full — obtained by *suspending the server process*, which keeps its socket open and simply stops
answering, the precise condition screen 06 is for — and for the recovery afterwards, which came back
with `SINCE YOU LOOKED · T3 > T5` and the controls live again.

**Not verified by running: the picker panel and its taps.** `Panel::SignalList`, the two-tap concede
and the toggle are wired and compiled and have never been pressed, for the same reason the five
buttons in ADR-038 have not been.

## Open questions

**The executable has no test project, and this is the second pass in a row where that forced a
throwaway harness.** Four libraries have one; `Lockstep` does not, so `ViewOf`, `OrdersOf`,
`ComposeSignals`, `DigestView`'s actor grouping and `FormatCountdown` — which had a real bug in it —
are reachable by no test at all. Adding a fifth test project changes the build shape
`Build/CheckProjectFiles.py` asserts and is an owner's decision, not one to take in the middle of a
feature. It is the next thing worth doing.

**Whether a lane offer should be reachable from the map.** The one-pager puts a trade lane in the
build menu with *Propose* where *Build* would be, and `BuildRow::isTradeLane` exists for exactly
that and is set by nothing. The signal list is the general answer; the map is the one a player
would find first.
