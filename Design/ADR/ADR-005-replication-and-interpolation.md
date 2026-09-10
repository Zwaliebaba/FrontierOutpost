# ADR-005 — The server sends a 32-byte state every tick; the client interpolates and never extrapolates

**Status:** Deprecated by ADR-015 — `ShipView` and the `ShipState` record were deleted on 2026-09-11. The principle it rests on (the client holds no rules and renders what the server told it) is carried forward in `MatchState`; the interpolation it specifies has nothing left to interpolate at four ticks a day.

**Date:** 2026-09-09
**Decided by:** Build session MVP-01, step 5.
**Supersedes:** —

---

## Context

The server ticks at 20 Hz and the client draws whenever the display lets it — measured on this
machine at about 165 frames a second, so roughly eight frames per tick. Something has to decide
what the client draws in between, and the client is not allowed to decide it by simulating
(MVP-01 §2: the client never moves the ship).

## Options considered

### A. Draw the last state received, unchanged, until the next one arrives

The simplest possible client. It is also 20 Hz motion on a 165 Hz display: the ship jumps eight
frames' worth every fiftieth of a second and stands still in between. At the speeds this ship
moves — 1200 mm a tick, which is about ten virtual pixels — that is a visible stutter rather than
a subtle one.

### B. Interpolate between the last two states

Hold the previous state and the latest, and draw between them over one tick's worth of real time.
Costs exactly one tick of latency — 50 ms — because the ship is drawn where it *was* a tick ago
sliding to where it is now.

### C. Extrapolate forward from the latest state using its speed and heading

Draw ahead of what the server has said, using the reported velocity. No added latency, and
smoother than B while the ship is doing something predictable.

It is wrong at exactly the moments that matter. The ship's whole behavior is arriving and
stopping (ADR-004), and an extrapolating client flies the ship past the target and then snaps it
back when the state that says "stopped" arrives. The same happens on every turn. A client that is
50 ms late and always right looks better than one that is on time and visibly wrong twice a
second.

## Decision

**The server sends a `ShipState` every tick**, unconditionally — no delta compression, no "only
when it changed". It is **32 bytes**: an 8-byte tick number, two 8-byte positions in millimetres,
a 4-byte speed, a 2-byte heading and 2 reserved bytes. At 20 Hz that is 640 bytes a second for one
ship. Sending unconditionally rather than on change is the right default at this size and makes a
lost or dropped state self-correcting rather than something the client has to reason about.

The tick number is in the record because the client needs it to notice a gap and because the
status line shows it — which is what makes "is the server running" answerable by looking.

**The client interpolates (B) and never extrapolates.** `Frontier::ShipView` holds the previous
state and the latest, and a fraction that runs 0 to 1 over one tick of real time from when the
latest arrived. Heading is interpolated through `ShortestTurnTurns16`, not as a number: 65000 to
500 is a turn of 1000 units forwards, and lerping the raw values would spin the ship most of the
way round the other way.

**The fraction is clamped at 1.** A client that stops hearing from the server freezes on the last
position it was told about rather than sliding past it. That is the same argument as against C,
applied to the failure case.

**On the first frame, before any state has arrived, the client draws no ship at all** and the
camera sits at the world origin. It is not drawn at the origin, because the origin is a position
nobody asserted and a ship that appears there and then jumps is worse than one that appears a
seventeenth of a second late. In practice this lasts at most one tick; the status line says
`WAITING FOR SERVER` so the state is visible rather than looking like a hang.

## Consequences

**What this makes easy.** The client has no rules in it. `ShipView` cannot advance the ship,
cannot predict and cannot move it because a key was pressed; the only thing that changes what it
says is a state arriving. That is the property MVP-01 §2 exists to protect, and putting the
arithmetic in one small class is what makes it checkable by reading.

**What this costs.** 50 ms of latency between the server computing a position and the client
drawing it, on top of whatever the transport costs — which over a loopback is nothing. For a
point-and-click game where the input is "go there eventually" this is not perceptible; for
anything with aiming it would be, and that is a different ADR.

**What it forecloses.** Client-side prediction, which is the thing a latency-sensitive game
eventually needs. Adding it later means giving the client a copy of the simulation and a
reconciliation step — a large change, and deliberately not one this MVP pays for.

**What is not decided.** How the client behaves when states arrive out of order or duplicated. Over
a loopback they cannot; over UDP they will. `ShipView::Accept` currently trusts what it is given.

## What this changes elsewhere

- **Code:** `NeuronCore/Protocol.h` is the record. `NeuronServer/Session.cpp` sends one a tick.
  `FrontierOutpost/ShipView.{h,cpp}` is the interpolation. `FrontierOutpost.cpp` drains the
  transport each frame and draws nothing before the first state.
- **AGENTS.md:** no change.
- **Design/:** no document superseded. ADR-006 covers what happens when the state queue is full.

## Open questions

Whether 20 Hz *looks* right under this interpolation, which `Design/Plans/MVP-01-IsometricShip.md`
§6 asks to be reported rather than voted on. Answered in the session report rather than here.

Out-of-order and duplicate states, as above. The tick number in the record is what a fix would be
built on, which is part of why it is there.
