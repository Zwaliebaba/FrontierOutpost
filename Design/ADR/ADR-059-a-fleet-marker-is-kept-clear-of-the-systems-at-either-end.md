# ADR-059 - A fleet marker is kept clear of the systems at either end

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, on a screenshot of two ordered moves: *"When I move a fleet, the triangle might move in the middle of the line as it now overlaps with the dot of Pell."*
**Supersedes:** - (bounds ADR-055)

---

## Context

ADR-055 made a fleet draw at the fraction of its lane its remaining ticks put it at, replacing a
fixed midpoint that was right only on a two-tick lane. That is the correct position and it is the
wrong PLACE at the ends of the range.

Two ways to land there. A move ordered and not yet locked is at progress zero - `HandleTap` sets it
when a destination is picked - so the marker, its arrowhead and its `FLT 1 - ETA T6` label are drawn
exactly on the node it is leaving. And the first tick of a four-tick crossing is a quarter along,
which on a lane 120 screen pixels long is still inside that node's disc.

The disc is not the wide part. The label is: `FLT 1 - ETA T6` is about fourteen glyphs drawn centred
on the marker, so it reaches ~56px either side. Clearing only the arrowhead moved the triangle off
the node and left the text running straight through it - which is what the first attempt at this
did, and the screenshot showed it.

## Decision

**A fleet marker is clamped to keep a label's half-width clear of both endpoints**, and a lane with
no room for that at both ends draws it in the MIDDLE.

The clamp is computed in screen pixels and applied to the design-space fraction: what has to be
cleared is drawn at a fixed size at every zoom - a node's radius, a label's glyphs - while the
lane's length in pixels changes with the camera. 68 pixels is half a label plus a capital's halo.

`DrawnProgress` is shared by the draw and the depth sort, because a fleet sorted from one point and
drawn at another is drawn in front of a system it is behind.

## Consequences

- On a long lane nothing changes: the clamp does not bind and ADR-055's progress is what is drawn.
- On a short lane the marker sits in the middle for the whole crossing. The ETA on the label is
  still exact, and the map was never the place the precise fraction was read from.
- **A fleet label can still collide with a nearby system's NAME.** The marker is clear of the
  node and its disc; two pieces of text at similar heights on a dense map is a label-layout
  problem this decision does not solve, and it is worth its own pass if it proves annoying.
- The clamp is drawing only. `Fleet::progress` in the view model is untouched, which is what
  `FleetRouteTests` pins.

## Verification

Run on the client, same seed and same camera, before and after: an ordered move to Xander drew its
arrowhead on Dothan's disc with the label across the node; it now draws at the midpoint of the lane
with both discs clear. The 451-test suite is unchanged and still passes - this moves pixels, not
decisions.
