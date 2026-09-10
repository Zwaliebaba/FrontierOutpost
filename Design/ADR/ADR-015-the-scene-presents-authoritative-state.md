# ADR-015 — The map is a 3D diorama of authoritative state; between ticks the client draws cosmetic time and sends only intent

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10.
**Supersedes:** —

---

## Context

The one-pager says the digest is the primary screen and the map is second, with commitments —
fleets in transit, open lanes, pending proposals — drawn as overlays. It does not say the map is
flat. The tree has an isometric renderer, two-tone flat shading, a mesh path, a starfield and a
touch-first gesture core, all proven (ADR-001, ADR-002, ADR-003, ADR-010), and the owner wants the
client to be a place worth looking at rather than a list with a diagram.

The constraint is `Design/README.md` §1: the server is authoritative and the client holds no
rules. Nothing happens between ticks. A scene that moves has to move for a reason that is not
simulation.

## Options considered

### A. A flat graph and text screens

Nodes, edges, labels. Cheap, legible, and it discards the renderer and the reason the game has a
look. It also gives the client no way to show a fleet *going* somewhere, which is the design's
central image.

### B. A 3D diorama, animated by cosmetic time, interactive by selection

The galaxy is drawn as an isometric scene on the ground plane at the generator's coordinates
(ADR-014): systems as meshes in the family of the station in the tree, lanes as thin quads,
fleets as ship meshes. Between ticks a fleet in transit is drawn at a point along its lane that is
a pure function of its departure tick, its arrival tick, the schedule and the wall clock — data
the snapshot carries and the client already has. Ships hold orbit, beacons blink. None of it is
state and none of it goes back to the server. Interaction is selection: a tap hits a system, a
fleet, a lane or a building, and an order is "this fleet to that system", never a coordinate.

It costs picking, a panning camera, a line path, an animation clock the simulation never sees,
and meshes. It also costs a rule that has to be held: the scene derives, and never produces.

### C. A 3D scene with client-side simulation for smoothness

Let the client advance fleets by its own clock and reconcile with the snapshot. It is B with a
second copy of the movement rules, which is exactly the divergence the architecture forbids, for
a smoothness that four ticks a day does not need.

## Decision

**B.** The map is a 3D diorama and it is the second screen, as the one-pager orders them.

**The scene is a derivation.** `Frontier::Scene`, in the game half of the client executable,
is built from four inputs and nothing else: the latest `VisibleSnapshot`, the seat's acknowledged
order book, the latest `Digest`, and the `TickSchedule` with the wall clock. It holds no rule and
no fact the server did not send. The position of a fleet on a lane is
`(now - departureTime) / (arrivalTime - departureTime)` of the way along, clamped, where both times
are the schedule's times for those ticks; a fleet that has arrived sits at the system whatever the
clock says. This is the one place the client reads the wall clock, and R16 does not reach it
because it is not the simulation.

**Overlays draw commitments.** A fleet in transit carries its ETA in ticks. The seat's own pending
orders are drawn as ghost fleets on dotted routes and as markers on the buildings they build. Open
proposals are countdowns on the lane they concern. Pending is visibly not resolved: ghosts are
drawn in the dark half of their pair (ADR-002) and never lit.

**Interaction is selection and intent.** `PointerInput` gains drag and long-press; the engine
gains picking — a target's screen bounds are the projection of its world bounds through ADR-003's
exact camera, and a tap resolves to the nearest target within a few pixels or to the ground.
Selecting a fleet and tapping a system submits `MoveFleet(fleetId, systemId)`; the server answers
(ADR-006). The client may highlight which systems are adjacent from the lanes it was sent, which is
a filter over data; it does not decide whether the order is legal.

**Three views, one scene.** The **map**, as above. The **system view**: a tap on a system zooms
into a close-up of that system — its station, the fleets present, its buildings drawn as unlock
state, with the build menu and the *Propose* buttons anchored to the things they concern — and no
placement, since buildings are unlocks and not floor plans. The **replay**: an engagement in the
digest plays its `EngagementLog` (ADR-013) as an exchange between the meshes present, round by
round, from the record and not from a rule. **The digest drives the camera:** each entry, when
tapped, calls `LookAt()` on what it concerns, so reading a tick is a flight over the diorama.

**The digest stays primary.** The client opens on it. The map is a tap away and is where orders
are placed. The one-pager's ordering is not reopened by this ADR; the map is made worth looking at,
not made first.

## Consequences

**What this makes easy.** The renderer, the shading, the camera, the starfield and the gesture
core are used rather than replaced. "Where the fleet is" — decision two of the three — is a thing
the player sees. The replay is the story of the match made visible at no cost to authority.

**What this makes hard.** The derivation rule is a discipline, and the way it fails is a helpful
engineer adding "just move the ghost when the order is accepted, before the server says so". The
test for it is structural: `Scene` has no method that takes a fleet and a position, and the only
message the map sends is an order with two ids in it.

**What it costs.** Picking, a line path, drag and long-press, a system-view camera, meshes for
systems and buildings, and a replay sequencer. The plan orders them: map and picking in the loop
MVP; system view and replay after the Phase 0 gate; the timeline scrubber — dragging the clock
forward to see where visible commitments will be — is a watch item after Phase 1 shows how far
ahead players plan (owner decision, 2026-09-10).

**What it forecloses.** Client-side prediction, deliberately; and a map that means something
different on two screens, since every client draws the same coordinates.

## What this changes elsewhere

- **Design/:** ADR-003 (panning camera), ADR-008 (zoom anchor), ADR-013 (the log the replay
  draws), ADR-014 (the coordinates). `Design/Plans/MVP-02-TheLoop.md` step 6.
- **Code:** nothing yet. `FrontierOutpost/ShipMesh.h` and `StationMesh.h` are the first two meshes
  of the family; `NeuronClient/MeshRenderer` draws them unchanged.

## Open questions

Legibility at 640×400 with sixteen colours: how many systems fit on screen at the zoom where a
whole empire is visible, and whether the fleet meshes need to be larger than the systems to be
tappable. Measured on a generated eight-seat galaxy at step 6; the size-per-player in `Rules` is
tuned after that figure exists, not before.

Whether cosmetic motion should pause when the window is not focused. It draws at display rate
today; the mobile reference notes what that costs on a phone, and on a desktop it is free.
