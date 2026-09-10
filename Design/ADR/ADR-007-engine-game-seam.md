# ADR-007 — The server owns a `Simulation`, not a `World`

**Status:** Deprecated by ADR-015 — `Simulation.h` was deleted on 2026-09-11. The seam it describes was shaped around `MoveToOrder`/`ShipState`; the rule it protects, that the engine knows nothing about this game (R9), is in AGENTS.md and is unaffected.

**Date:** 2026-09-09
**Decided by:** Build session MVP-01, step 5. Not one of the ADRs the plan asked for; written because the decision had to be made mid-implementation and it has real alternatives (`Design/README.md` §5).
**Supersedes:** —

---

## Context

`Design/Plans/MVP-01-IsometricShip.md` step 5 says:

> `NeuronServer`: a `Session` that owns a `World`, receives orders from a transport, ticks on the
> schedule from the threading ADR, and pushes `ShipState` back.

`AGENTS.md` §2 says:

> **`GameLogic` is referenced by the executable and by nothing else.** It is server-side game
> code; the day a client-side file reaches for it is the day the server stopped being
> authoritative.

and gives the dependency graph in which `NeuronServer.lib` references `NeuronCore` and nothing
more.

Both cannot be true as written. A `Session` that owns a `Frontier::World` is a `NeuronServer` that
links `GameLogic`, which is the edge the repository map forbids. This is the contradiction that
step 0 of the plan asks a session to find and report, and it is reported here rather than resolved
silently.

Two further facts constrain the answer. `NeuronCore` is the only library both halves of the
process share. And `FrontierOutpost.exe` references all four libraries, so it is the one place
that is allowed to know about the engine and the game at once.

## Options considered

### A. Let `NeuronServer` reference `GameLogic`

Implement the plan's sentence literally: add `$(SolutionDir)GameLogic` to `NeuronServer`'s include
directories and a project reference, and have `Session` hold a `Frontier::World`.

It is the least code. It also deletes the rule: once the engine's server links the game, there is
no mechanical difference between engine and game any more, and `AGENTS.md` §2's line about the day
the server stops being authoritative becomes a comment rather than a constraint. It would also
mean `NeuronServerTests` links `GameLogic`, so a test of the tick schedule would fail when the
ship's kinematics changed.

### B. Make `Session` a template on the simulation type

`Session<Frontier::World>`, instantiated by the executable. No virtual calls, no abstraction, and
the dependency graph is untouched — the engine genuinely never names the game.

It costs putting the whole of `Session` in a header, including its thread and its schedule, which
is the part of the server most likely to grow. And it makes the type of a session depend on the
game, so anything holding one has to be a template too. For a class whose job is to own a thread
and a clock, that is the tail wagging the dog.

### C. Declare an abstract `Simulation` in `NeuronCore`

`Session` owns a `std::unique_ptr<Neuron::Simulation>`. `Frontier::World` implements it.
`FrontierOutpost.exe` puts the two together. The engine's server still owns the simulation and
still ticks it; it just does not know what it is.

Costs one virtual call per tick — twenty a second — and one indirection.

## Decision

**C.** `NeuronCore/Simulation.h` declares an abstract `Simulation` with `ApplyOrder`, `Tick` and
`Snapshot`. `NeuronServer::Session` owns one. `GameLogic`'s `World` implements it.
`FrontierOutpost.cpp` calls `session.Start(std::make_unique<Frontier::World>(), transport)`.

Both documents end up true: the plan's `Session` owns and ticks the simulation, and `NeuronServer`
still references `NeuronCore` and nothing else.

**On R2.** `AGENTS.md` R2 warns that a base class with one derived class is ceremony, and this ADR
is deliberately not that. The abstraction is not there to anticipate a second implementation, it
is there because the alternative is an edge in the dependency graph the repository map forbids —
the interface is load-bearing rather than speculative. It is worth adding that the second
implementation arrived immediately and in the place that shape usually pays off:
`NeuronServerTests` has a `CountingSimulation`, which is how the tick schedule, the order-before-
tick ordering and the once-per-tick replication are tested without a ship anywhere in sight.

**On where the wire records live.** `MoveToOrder` and `ShipState` are in `NeuronCore`, which is
what the plan asks for and which this seam depends on — `Simulation`'s signature names both. It is
also the one place in the tree where the engine has a game noun in it, and R9 says the engine
knows nothing about this game. That tension is real and is left standing rather than resolved,
because the plan is explicit and because generalizing the two records — an entity state, a move
order — is a rename rather than a redesign on the day a second kind of thing needs to move. It is
noted in `Protocol.h` so the next reader meets it there rather than deducing it.

## Consequences

**What this makes easy.** `NeuronServerTests` tests the server without a game. `GameLogicTests`
tests the game without a server, a window or a thread. Neither suite is coupled to the other's
subject, which is what makes them cheap to keep passing.

**What this makes hard.** The simulation is now behind a pointer, so the server cannot ask it
anything the interface does not expose. Adding a question means widening an interface in
`NeuronCore` that both halves compile against — which is friction, and is mostly the good kind:
it is exactly the moment to ask whether the server should be asking.

**What it costs.** A virtual call and an indirection twenty times a second, which is not a cost.
And an abstract class that a reader has to follow one hop to understand, which is.

**What it forecloses.** Nothing yet. If the server ever needs to tick many simulations, or a
simulation needs to be sharded across threads, this is the interface that changes.

## What this changes elsewhere

- **Code:** `NeuronCore/Simulation.h`, `NeuronServer/Session.{h,cpp}`, `GameLogic/World.h`,
  `FrontierOutpost.cpp`.
- **Design/:** `Design/Plans/MVP-01-IsometricShip.md` step 5's "a `Session` that owns a `World`"
  is implemented as "a `Session` that owns a `Simulation`, which the executable makes a `World`".
  The plan is archived by this session, so the departure is recorded here rather than by editing
  it.
- **AGENTS.md:** no change. §2's dependency graph is what this ADR preserves.

## Open questions

Whether `MoveToOrder` and `ShipState` should be generalized out of game vocabulary, as above. Not
now; it is a rename, and doing it before a second kind of entity exists would be guessing at what
the general case looks like.
