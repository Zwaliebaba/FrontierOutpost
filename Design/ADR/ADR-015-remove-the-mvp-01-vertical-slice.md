# ADR-015 — The MVP-01 vertical slice is deleted, not kept for reference

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Owner decision, 2026-09-11, choosing between the three scopes offered in the session that built the main page.
**Supersedes:** —

---

## Context

ADR-014 made `FrontierOutpost.exe` open the main page — the ops console of the async 4X the
one-pager describes. It left the MVP-01 vertical slice in the tree: an isometric ship you clicked
to move, a station, a procedural starfield, and everything under them — a graph-free simulation of
one ship, a wire protocol carrying two ship-shaped records, a loopback transport, an authoritative
session ticking at 20 Hz, and the engine/game seam that joined them.

That slice did its job. It is what MVP-01 was for: proving the D3D12 client, the tick loop, the
transport and the client/server split against something that moved on screen. None of it is
reachable from the executable any more, and the game it was proving is not the game being built —
this one is asynchronous at four ticks a *day*, its world is a graph rather than a plane of world
units, and its client draws an interface rather than a scene.

The question is what to do with roughly 2,000 lines that compile, pass tests, and describe a
different game.

## Options considered

### A. Keep it, unreachable

The state ADR-014 left. It costs nothing at runtime — the linker drops what the executable does
not call — and it keeps a working reference for how the tick loop, the transport and the seam were
put together.

It is rejected because unreachable code is not a reference, it is a claim. Every one of those files
carries comments in the present tense about a design the tree no longer has, four test suites
assert the behaviour of a simulation nobody runs, and `AGENTS.md` §2's map describes an
architecture that no longer connects to anything. A reader cannot tell, from the tree, which half
is the game. Git is the reference; the working tree is supposed to be what is true.

### B. Delete the scene, keep the scaffolding

Remove the ship, the station, the 3D mesh and camera and starfield rendering, and the ship
simulation — but keep `Session`, `LoopbackTransport`, `MessageQueue`, `Simulation.h`, `Protocol`
and `Trigonometry`, on the grounds that the 4X needs a tick loop, an authoritative server and a
transport, so most of that is scaffolding that would only be rebuilt.

This is the conservative choice and it was the recommended one. What argues against it is that the
scaffolding is not neutral: `Protocol`'s two records are `MoveToOrder` and `ShipState`, which are
ship vocabulary and would be replaced wholesale; `Session` ticks at a rate chosen for a real-time
demo; `Simulation`'s interface is `ApplyOrder(MoveToOrder)`, `Tick()`, `Snapshot() -> ShipState`,
every parameter of which is about the removed game. Keeping it means keeping a seam shaped for
something else and calling it a head start.

### C. Delete everything the executable does not use

Option B plus the scaffolding. Two of the nine projects — `GameLogic` and `NeuronServer` — are left
holding only their umbrella and precompiled headers, and three test suites are left holding only a
placeholder.

The cost is real and should not be understated: ADR-005, ADR-006 and ADR-007 were carefully
reasoned and their implementations were correct. Rebuilding a transport and an authoritative loop
for the 4X will re-derive some of what they decided. What it buys is a tree in which everything
present is either used or obviously waiting, and a set of design documents whose status lines say
plainly which decisions are live.

## Decision

**Option C.** Thirty files are deleted:

- **The scene:** `FrontierOutpost/{ShipMesh,StationMesh,ShipView}.*`,
  `NeuronClient/{MeshRenderer,Mesh,IsometricCamera,Starfield}.*` and
  `NeuronClient/Shaders/{Mesh,Starfield}{VS,PS}.hlsl`.
- **The simulation:** `GameLogic/{Ship,World}.*`.
- **The scaffolding:** `NeuronCore/{Protocol,LoopbackTransport,MessageQueue,Simulation,Trigonometry}.*`
  and `NeuronServer/Session.*`.

`GameLogic` and `NeuronServer` keep their umbrella headers, their precompiled headers and their
place in the solution. **They are empty on purpose and they stay:** the 4X has an authoritative
server and a server-side simulation in its design, and the projects, their references and their
test suites are where those go. Deleting the projects would mean re-deciding §2's architecture,
which this ADR does not do.

The three suites left with nothing get their `SuiteSmoke` placeholder back. AGENTS.md §3 explains
why that matters and it is exactly this case: vstest reports "no tests found" as a pass, so an
empty suite is a green check mark over a library nobody exercised.

**What survives, and why.** `NeuronClient` keeps `Device`, `SceneTarget`, `DescriptorHeap`,
`FontRenderer`, `ShapeRenderer`, `PointerInput`, `Color`, `Font` and `D3D12Defaults` — the
platform, the framebuffer and the interface, none of which was MVP-01-specific. `PointerInput`
keeps its zoom handling even though nothing consumes zoom steps today: it is the measured answer
to how a wheel arrives (ADR-009), the main page's map is specified as pan/zoom, and the code that
would be deleted is the part that was hard to get right.

## Consequences

**What this makes easy.** The tree is now readable as one thing. Every file in it is either part of
the main page, part of the platform under it, or an empty project with a name that says what goes
there. `AGENTS.md` §2's dependency diagram is true again.

**What this makes hard.** The next session that needs a tick loop, a transport or a wire format
starts from the design documents rather than from working code. That is the accepted cost, and the
mitigation is that ADR-005, ADR-006 and ADR-007 are *deprecated rather than deleted* — the
reasoning is intact and findable, and a future ADR that re-decides transport or replication should
read them first and say what it takes and what it leaves.

**What it costs.** Test count drops from 108 to 41, measured on 2026-09-11. The sixty-seven that
went were about ship kinematics, the isometric camera, the starfield's scroll arithmetic,
fixed-point trigonometry, wire serialization, transport queues and the session thread — real tests
of real code, none of which exists any more. Three of the forty-one that remain are `SuiteSmoke`
placeholders standing in for suites with nothing to test. The number going down is the change
being honest, not coverage being lost.

**What it forecloses.** Nothing that was wanted. The MVP-01 slice is in git at `db6b8ba` and
before, and `Design/Archive/MVP-01-IsometricShip.md` still describes what it was and why.

## What this changes elsewhere

- **Code:** the thirty files above. Project files and filters updated; `NeuronClientTests` loses
  its camera and starfield suites; `GameLogicTests`, `NeuronCoreTests` and `NeuronServerTests` are
  reduced to `SuiteSmoke`.
- **AGENTS.md:** §2's repository map now says what `GameLogic` and `NeuronServer` currently hold.
- **Design/:** ADR-003, ADR-004, ADR-005, ADR-006, ADR-007, ADR-010, ADR-012 and ADR-013 are
  **Deprecated** — each decided something about code that no longer exists. Their status lines
  name this ADR. ADR-001, ADR-002 and ADR-008 were already superseded and are left alone.
  ADR-009, ADR-011 and ADR-014 stand: they are about input, the framebuffer and the interface,
  all of which survive.
- **Design/Archive/MVP-01-IsometricShip.md:** unchanged. It is the record of what was built and
  believed then, and `Archive/` is not authoritative (Design/README.md §2).

## Open questions

What the 4X's wire protocol and tick loop look like. They are a design session, and the deprecated
ADRs are its reading list rather than its starting point.

Whether `PointerInput`'s zoom should drive the main page's map. `Design/Screens/README.md` says the
map pans and zooms and that the perspective is fixed; neither is implemented, and the producer
half is already there.
