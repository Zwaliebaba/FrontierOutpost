# ADR-018 — The simulation is a pure function, and its randomness is pinned by value

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session for `Design/Plans/4X-01-CoreLoop.md`, Step 0. The plan asks for this ADR before any code because it constrains what Steps 1 and 2 are allowed to be built out of.
**Supersedes:** —

---

## Context

`AGENTS.md` R16 already says `GameLogic` is integer arithmetic, `/fp:precise`, no `float` where an
integer will do, no wall clock, and no iteration over an unordered container that reaches the
simulation. It was written for a ship whose position was in millimetres, and it was framed as a
property worth *having* — "two builds of the same simulation disagreeing about the same sum with no
line to blame".

The 4X needs it to be more than that. Three things the one-pager and the test plan ask for are only
reachable if the simulation is a pure function of its inputs:

**The galaxy is generated and must be reproducible.** The one-pager's generator has constraints a
seed can fail — "a seed that can't satisfy this is rejected" — so generation is search, and a search
whose result depends on the machine is a match that is not the same match for the twelve people
playing it.

**Combat is "deterministic: uncertainty comes from what humans ordered, not from dice"**, and
"processing order can never change an outcome". That is not a quality goal, it is a rule of the
game. A player who reads the map and computes what a fight will do must be right.

**The test plan runs three-week matches with strangers, and Phase 0 is explicitly about finding
broken mechanics.** A bug report from a player is "my fleet died and I don't know why". The only
way that is actionable is if the tick can be re-run.

There is also a payoff this ADR does not collect and only makes possible: a match that is a pure
function of a seed and a list of orders can be *persisted* as that seed and that list, and reloaded
by re-resolving. `4X-02` decides whether to take it.

## Options considered

### A. Determinism by discipline

Follow R16 carefully, review for it, and trust that careful code stays careful. This is what the
tree does today, and for MVP-01 it was right: one ship, one machine, no reproduction requirement.

It fails here for a reason that is structural rather than about diligence. Non-determinism in a
simulation is silent — it produces a plausible answer, just a different one — and there is no
moment at which anybody notices. A `std::unordered_map` iterated into a damage calculation is a
correct-looking line that a reviewer has no reason to stop on. Discipline catches what it is
looking for and this is not visible.

### B. Determinism as a pinned, tested property

State it as a contract, give it mechanisms, and *test* it: the resolver and the generator are pure
functions with no hidden inputs; the PRNG is specified by value rather than by name; every
iteration whose result reaches state is over a sorted order; and a hash of the state after N ticks
is asserted against a known value.

The cost is real. Every container whose order could matter needs a defined order, which means
`std::vector` and sorted lookups where a hash map would be the obvious choice. Every place that
wants a random number needs one threaded to it rather than drawn from the air.

### C. Lockstep with checksums, verified at the network layer

Let the simulation be whatever it is, and detect divergence by exchanging state hashes between
clients each tick.

Rejected because it detects the problem instead of preventing it, and the detection arrives at the
worst possible time: mid-match, with twelve people, and nothing to do about it. It is also solving
a problem this architecture does not have — the server is authoritative (`AGENTS.md` §2), so
clients do not simulate and there is nothing to keep in step.

## Decision

**B.** Determinism is a contract of `GameLogic`, and it has four mechanisms.

**1. No hidden inputs.** The generator is a pure function of `(MatchRules, seed)`. The resolver is
a pure function of `(state, order sets)`. Neither reads a clock, a file, an environment variable,
an address, or anything else that differs between two runs. The tick number, and whether a player
was present, are things the *server tells* the simulation — parameters, not observations. R16
already forbids the clock; this widens it to the general case and says why.

**2. The PRNG is pinned by value, not by name.** `Neuron::Prng` is **splitmix64**, written out:
a 64-bit state advanced by the golden-ratio odd constant, then two xor-shift-multiply rounds and a
final xor-shift. Every operation is on `std::uint64_t` and every constant is in the source.

The alternative — `std::mt19937` with `std::uniform_int_distribution` — looks more respectable and
is unusable here. The *engine* is specified by the standard; the **distributions are not**, so the
same generator and the same seed produce different draws on different standard libraries. A galaxy
that differs between a machine built with MSVC and one built with libstdc++ is exactly the failure
this ADR exists to prevent, and `Design/Reference/mobile-portability.md` makes that a live concern
rather than a hypothetical one.

Bounded draws use **rejection**, not modulo: for a bound `b`, values below `2^64 mod b` are
redrawn, so every outcome is equally likely. Modulo alone biases toward the low end, by an amount
too small to see and large enough to make a generated galaxy quietly unfair.

`Prng` is in `NeuronCore` because it is arithmetic with no game in it, and its sequence is pinned
by value in `NeuronCoreTests`. **That test is a wire format in all but name**: changing the PRNG
changes every galaxy ever generated, so the test failing is the point.

**3. Order is explicit wherever it reaches state.** Anything iterated to produce a result is
iterated over a `std::vector` in index order, or over a container sorted by id first. No
`std::unordered_map` or `std::unordered_set` feeds a resolution. Where the order genuinely does not
matter — because the operation is commutative and associative over integers — the code says so in a
comment, so that the next reader does not have to re-derive it.

**4. Identity is an index, and indices are typed.** `Neuron::Id<Tag>` wraps a `std::int32_t` index
into the array that holds the thing. Indices are stable within a match, cheap to sort, and free of
the ordering questions a pointer or a generated key would bring. The tag makes a `LaneId` and a
`SystemId` different types, which costs nothing at run time and removes an entire class of silent
bug: both are indices, both are plausible in the same expression, and the compiler now objects.

**What "pure" does not mean here.** It does not mean functional style, immutability, or copying
state per phase for its own sake. The resolver mutates a working copy. What it means is that the
*result* is a function of the inputs and nothing else.

## Consequences

**What this makes easy.** Re-running a tick, which is the whole of Phase 0's ability to answer "why
did that happen". Testing rules without a network, a clock or a client — `GameLogicTests` can play
a whole match. Asserting that a change to unrelated code changed nothing, by hashing. And the
option, which `4X-02` may take, of a match file that is a seed and a list of orders.

**What this makes hard.** Lookups. `SystemId -> something` wants to be a hash map and will be a
vector indexed by id, or a sorted vector searched. At the sizes here — tens of systems, tens of
fleets — that is faster anyway, but the habit has to be broken deliberately rather than discovered.

Floating point is also now effectively banned in `GameLogic` rather than discouraged. R16 said "no
`float` where a fixed-point or integer quantity will do"; in practice, for this game, one always
will. Positions are integers in design units because they are *drawn* rather than simulated, and
the drawing side converts.

**What it costs.** Every random draw needs a `Prng&` threaded to it. The generator takes one; the
resolver, if it ever needs randomness, takes one seeded from the tick and the match seed so that
re-resolving a tick draws the same numbers. Nothing calls a global.

**What it forecloses.** Any library that wants to do arithmetic for us, which R14 already
forecloses. Parallelising a resolution phase across threads, unless the parallel decomposition is
itself deterministic — worth stating because it is the optimisation somebody will reach for, and at
twelve players there is nothing to optimise.

## What this changes elsewhere

- **Code:** `NeuronCore/Prng.{h,cpp}` and `NeuronCore/Id.h` are new and exist because of this ADR.
  `GameLogic` is written under it from its first line.
- **AGENTS.md:** no change. R16 already says the operative things; this ADR is why they matter for
  this game and what mechanisms carry them.
- **Design/:** nothing superseded. `4X-01` §3 asks for this and its §7 checklist has the row.

## Open questions

Whether the resolver needs randomness at all. The one-pager's combat is deterministic from
strengths and a defender bonus, with no dice anywhere, so it may not. If it does — a tiebreak, a
salvage roll in Phase 2's Exile work — the draw is seeded from `(matchSeed, tick)` so that
re-resolving a tick is the same tick, and this ADR is where that rule is written down.

What the state hash covers. Step 9 needs one; whether it is over the whole state or a stated subset
is a decision for the step that writes it, and it should be written down there, because a hash that
silently omits a field is a determinism test that passes while the bug ships.
