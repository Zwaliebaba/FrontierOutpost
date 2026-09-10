# ADR-004 — Ship kinematics: turn-rate-limited heading, and a deceleration that arrives stopped

**Status:** Deprecated by ADR-015 — `GameLogic/Ship` was deleted on 2026-09-11. The 4X moves fleets along lanes at authored tick costs, so nothing here describes the tree any more.

**Date:** 2026-09-09
**Decided by:** Build session MVP-01, step 5. Recommendation stated in `Design/Plans/MVP-01-IsometricShip.md` §3; this ADR takes it and fixes the units, the ranges and the arithmetic it left open.
**Supersedes:** —

---

## Context

The tick is 20 Hz, settled by owner decision on 2026-09-09 (`Design/Plans/MVP-01-IsometricShip.md`
§2). This ADR is about what happens inside one.

Two constraints bind hard. R16 says `GameLogic` uses no `float` where an integer or fixed-point
quantity will do, no wall clock, and no iteration order that reaches the simulation — because the
symptom of losing determinism is two builds of the same simulation disagreeing about the same sum
with no line to blame. And space is unbounded (MVP-01 §2), so there is no play area to clamp
positions into and the range of whatever type holds a position is a real question rather than a
formality.

## Options considered

### A. Teleport along a straight line

Move the ship towards the target by a fixed step each tick. It is what the MVP would look like if
nobody decided anything, and it is written down here because it is the thing that gets built by
accident.

It has no heading, so the mesh has nothing to point along; it has no speed, so there is nothing to
interpolate that is not just position; and it makes the ship's arrival instantaneous, which means
the click and the stop are the same event and neither can be seen to be wrong.

### B. Turn-rate-limited heading, acceleration to a top speed, deceleration to arrive stopped

The ship has a heading it can only change so fast, and a speed it can only change so fast. Going
somewhere is therefore three things at once: turn towards it, get up to speed, and start braking
in time to stop on it.

It costs an arrival test that has to be exact, and it introduces a case A does not have — a target
behind the ship, where turning while accelerating carves a wide arc away from where the player
pointed.

## Decision

**B**, with these quantities. Every one is an integer and every one carries its unit in its name
(R6).

| Quantity | Type | Value | In human terms |
|---|---|---|---|
| Position | `std::int64_t` millimetres | — | ±9.22e18 mm ≈ ±0.97 light-year |
| Heading | `Neuron::Turns16` (`std::uint16_t`) | — | 65536 units to a full turn |
| Speed | `std::int32_t` mm/tick | max 1200 | 24 m/s |
| Acceleration | `std::int32_t` mm/tick² | 60 | top speed in 20 ticks — one second |
| Turn rate | turns16/tick | 1024 | 5.625°/tick, 112.5°/s, a half turn in 1.6 s |
| Facing tolerance | turns16 | 8192 | 45° |

**Millimetres in an `int64`, and what happens at the edge.** The range is about 0.97 light-years
either way. At top speed the ship covers 24 m/s, so reaching the end of the range takes 2.4×10^10
years — longer than the universe has existed, and that is before noticing that a player would have
to fly in one direction for all of it. It is not reachable. It is nevertheless **saturated**
rather than left to overflow (`Neuron::SaturatingAdd`), because signed overflow is undefined
behavior today and "unreachable" is not a defence against undefined: the cost is one compare per
axis per tick and it converts a program that is undefined into one that is merely stuck.

**Angles are turns16, not radians.** A full turn is a whole number of units, so a heading wraps by
unsigned overflow with no accumulated error and no normalization step anyone can forget, and
"the short way round" is a subtraction reinterpreted as signed (`ShortestTurnTurns16`) rather than
a case analysis about crossing zero. Sine, cosine and arc tangent come from CORDIC in
`NeuronCore/Trigonometry.h` — shifts and adds, no floating point, measured error 19 of 65536 for
sine and cosine and 9 units for the arc tangent.

**The tick, in order:** turn towards the target by at most the turn rate; choose a speed; move.

Choosing a speed is where the two interesting cases are. The ship **brakes** when the distance
left is at or below its stopping distance, and it **does not accelerate at all** while its heading
is more than the facing tolerance away from the target. That second rule is what stops a ship
ordered to somewhere behind it from carving a wide arc: it slows, turns on the spot, and then
goes.

The stopping distance is a closed form, `(n-1)·v − a·(n-1)·n/2` where `n = ceil(v/a)`, and the
test suite checks it against the loop it replaces **at every speed the ship can be at**, because
it is exactly the sort of algebra that is wrong by one term without looking wrong.

**Arrival is exact.** When the distance left is at or below one tick's travel, the ship is placed
*on* the target, its speed set to zero and the order cleared. Without that it oscillates around
the target by a few millimetres a tick forever, which at 20 Hz is a visible jitter rather than a
rounding detail. `GameLogicTests` asserts arrival is exact — the position equals the target, not
approximately — in sixteen directions, and separately that the ship never goes past the target and
comes back.

## Consequences

**What this makes easy.** The whole of it is testable with no D3D12, no window and no threads: a
`Ship` is arithmetic. Nineteen tests in `GameLogicTests` cover turning, arriving, not overshooting,
the speed bounds, the deceleration formula and determinism, and they run in 0.2 seconds.

**What this makes hard.** Every quantity now has a unit that has to be converted at a boundary.
Millimetres to metres for the renderer, turns16 to radians for the world matrix. Those conversions
are real places to be wrong, and the names are the mitigation.

**What it costs.** Integer trigonometry is a page of CORDIC that a `std::sin` call would replace.
That is the price of R16, and the page is written once.

**What it forecloses.** Nothing about the ship is tunable at runtime; the constants are compile
time and a different ship is a different set of them. When a second kind of ship arrives, these
move from `static constexpr` on `Ship` to a class the ship refers to — and that is a change with
no arithmetic in it.

## What this changes elsewhere

- **Code:** `GameLogic/Ship.{h,cpp}` is this ADR. `NeuronCore/Trigonometry.{h,cpp}` is the integer
  maths it needs. `GameLogic/World.cpp` reports the result.
- **AGENTS.md:** no change. R16 is what this implements.
- **Design/:** no document superseded.

## Open questions

Whether the facing tolerance should scale with speed. At 45° a ship at top speed still turns
inside its own stopping distance, but a faster ship would not, and the rule would need to become
"do not accelerate towards a heading you cannot reach before you would have to brake". Not needed
at 24 m/s; noted because the first speed increase will need it.

Nothing here decides what happens when two ships want the same place. There is one ship.
