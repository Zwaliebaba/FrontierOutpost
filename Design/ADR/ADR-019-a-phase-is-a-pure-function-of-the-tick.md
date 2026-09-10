# ADR-019 — A phase is a pure function from one state to the next

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session for `Design/Plans/4X-01-CoreLoop.md`, Step 4. The plan asks for this decision in the step that meets it.
**Supersedes:** —

---

## Context

The one-pager fixes six phases in a fixed order — lock, production and research, movement, combat,
claims and captures, digest — and adds a sentence that is easy to read past and expensive to get
wrong:

> Every phase reads the tick-start state and writes the next; no order reads another's write within
> a phase.

That sentence is doing two separate jobs. The first is **fairness**: if the tenth fleet processed
sees a world the first fleet has already changed, then the player whose fleet happens to be tenth
is playing a different game, and the ordering of a `std::vector` becomes a game rule nobody voted
for. The second is **determinism**: ADR-018 makes the resolver a pure function of (state, orders),
and a phase that reads its own partial output has as many possible results as it has orderings.

The one-pager's central consequence depends on it. *Movement precedes combat, so leaving beats
arriving* — a fleet ordered out is gone before the fight it was ordered out of. That only holds if
movement resolves against the positions everybody had at the start of the tick. If departures and
arrivals were interleaved with combat, "did my fleet get out" would depend on iteration order.

`Design/Plans/4X-01-CoreLoop.md` Step 4 states the test bluntly: *if the resolver's signature makes
it possible to read what another order wrote in the same phase, the signature is wrong.* So this is
a decision about a **type signature**, not about a coding convention.

## Options considered

### A. Mutate one `Match` in place, and be careful

`void Phase(Match& _state, ...)`. Each phase edits the state it was handed. The no-read rule is a
comment, and every future contributor has to know that reading `_state` inside a loop that also
writes it is a bug rather than the obvious thing to do.

Cheapest to write and impossible to enforce. Nothing about the signature distinguishes a legitimate
read of a field this phase does not touch from a read of a field the previous iteration just wrote,
so the rule can only be checked by reviewing every line — forever, including the lines written by
whoever adds trade lane cancellation in eighteen months. It also makes the "resolve the same tick
twice" test hard to write, because there is no second state to compare against.

### B. Two states, threaded explicitly

`void Phase(const Match& _in, Match& _out, ...)`. Reads come from `_in` and writes go to `_out`,
and the compiler enforces the first half.

Correct, and it has a trap: `_out` starts as a copy of `_in`, so a phase that writes a field and
then reads it back from `_out` is still possible and still compiles. It also makes every phase
responsible for copying forward everything it did not change, which is a class of bug — a forgotten
field silently resets each tick — that has nothing to do with the rules being modelled.

### C. Each phase returns the next state

`[[nodiscard]] static Match Phase(const Match& _in, ...)`, with `Match next = _in;` as the first
line and `return next;` as the last.

The read path is `const` and the write path is a local. Copying forward is automatic, so a phase
cannot forget a field. The resolver is then literally a fold: `state = Lock(state); state =
Produce(state); ...`. It costs one copy of a `Match` per phase.

### D. Event sourcing: phases emit deltas, applied between phases

Each phase returns a list of changes; a separate applier folds them in. This is the purest form and
it makes conflicts explicit — two orders that both want a system produce two deltas, and something
has to arbitrate.

Rejected as premature. It needs a delta type for every kind of change, a conflict rule per delta
kind, and a debugger story for "why did this delta not apply". The one-pager's conflicts are
already resolved by rules that read cleanly as code — claims need presence uncontested, sieges need
two consecutive ticks — and expressing them as delta arbitration would obscure them. If a later
phase genuinely needs conflict arbitration, this is the option to revisit.

## Decision

**C. Every phase is `Match(const Match&)`, and the resolver is the composition of six of them.**

`TickResolver::Resolve` takes the tick-start state and the order sets and returns the next state.
Each private phase takes a `const Match&` and returns a new one. Within a phase, every read is of
the parameter and every write is to the local copy, so the last order processed sees exactly what
the first one saw.

Four things follow, and each is load-bearing:

**Combat is a phase that runs and does nothing** rather than a phase that is not there yet. Step 5
fills it in. Keeping it in the sequence from the first tick ever resolved means the ordering around
it — movement before it, claims after it — is under test before the code exists, and a `TickLog`
written today has the same six entries as one written after step 5.

**Every loop that reaches the result runs over an index, ascending.** Players, fleets, systems,
lanes. Never a set, never a map, never "whichever we found first". This is ADR-018's requirement
applied to the resolver, and it is what makes two machines agree about a tie.

**Order sets are applied in player order**, whatever order the transport delivered them in, and
each is validated against the *tick-start* state — the state the player was looking at when they
decided. Validating against a state other players have already changed would refuse an order that
was legal when it was given.

**The resolver's second output is a `TickLog`.** It is not optional and not a debug aid: the
client's *Replay tick N* reads it, and the step's tests assert against what the log says happened
rather than only against the state that came out. A rule that fires correctly but silently is a
rule nobody can debug at four ticks a day.

## Consequences

**It costs five `Match` copies per tick.** A twelve-player match is sixty-one systems, a few dozen
fleets and a handful of proposals — a few kilobytes, four times a day. Measured against what it
buys, which is a fairness rule that cannot quietly stop being true, this is not a cost worth
optimising. If a profile ever says otherwise, the answer is option B with a discipline test, not a
return to option A.

**`Match` has to be a value.** No pointers into itself, no shared state, nothing that a copy would
alias. That is already true and is now a requirement rather than an accident, checked by
`Match::IsConsistent`.

**The resolver cannot be incremental.** There is no "resolve just this fleet" entry point, and
there should not be one — a partial resolution is a state no rule was written against.

**Tests can build the state they want directly.** `Match` exposes its vectors, and the step 4 tests
use that to put a fleet mid-siege rather than playing twelve ticks to reach one. That is a
deliberate trade: the alternative is tests that fail for reasons unrelated to what they are
testing.

## What this changes elsewhere

Nothing yet. `4X-02` will wrap `TickResolver::Resolve` in a server that decides *when* to call it;
this ADR fixes only what happens when it is called. The persistence payoff ADR-018 named — a match
as a seed and a list of order sets, reloaded by re-resolving — needs this to hold, because a replay
is a re-run.

## Open questions

**Where the `TickLog` lives once the match is over.** It is produced per tick and currently
discarded by every caller but the tests. *Replay tick N* needs at least the last one, and the
one-pager's digest is per tick, so something has to keep them. That is a persistence question and
belongs to `4X-02`.

**Whether the six-phase list survives the region.** The sealed region opens at a known tick and
does nothing in Stage A. When it does something, it is not obvious which phase it belongs to.
