# ADR-019 — The generator's randomness is our own SplitMix64, not the standard library's

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session, MVP-02 slice 1. Made mid-implementation and written up because it has real alternatives (`Design/README.md` §5).
**Supersedes:** —

---

## Context

ADR-004 makes the galaxy a function of a seed, and ADR-012 stores that seed rather than the galaxy:
a match directory holds the seed and the per-tick states, and replaying a tick means regenerating
the galaxy from the seed and running the resolver over it. ADR-014 adds that the generator may
reject a seed, so the rejection itself has to be reproducible too.

All of that rests on one property: **the same seed produces the same galaxy on every machine that
will ever run this game.** A Phase 0 report of "my fleet vanished at tick 31, seed 4815" is only a
bug report if the galaxy comes back. If it does not, the seed in the state is decoration.

R16 already bans floating point and unordered iteration from `GameLogic` for the same reason.
Randomness is the third way a simulation stops agreeing with itself, and it is the one the rule did
not name.

## Options considered

### A. `std::mt19937_64` with `std::uniform_int_distribution`

The obvious answer, and the engine is genuinely fine: the standard specifies Mersenne Twister's
output bit for bit, so `mt19937_64` from a given seed is the same everywhere.

**The distributions are not specified.** `std::uniform_int_distribution` is required to produce a
uniform result and nothing more — how many words it consumes, and how it maps them onto the range,
is each implementation's business, and libstdc++, libc++ and the MSVC standard library genuinely
differ. So the same seed gives a different galaxy under a different standard library, and the
difference is invisible until somebody compares two machines. That is a poor property for a value
whose entire purpose is reproducibility, and it would quietly become a portability blocker the day
anything is built with Clang.

### B. `std::mt19937_64` with our own range reduction

Keep the specified engine, write the two lines that turn a word into a bounded value. Reproducible,
and standard where it matters.

It costs a dependency on `<random>` for an engine whose 2.5 kilobytes of state and 312-word refill
are sized for statistical work this game does not do, and it still leaves a reader to notice that
the distribution was deliberately not used.

### C. SplitMix64, written out

Sixteen lines: an additive Weyl step by the golden-ratio constant, two xor-shift-multiply rounds, a
final xor-shift. It is the standard seeding routine for the xoshiro family, its constants are
published and fixed, and its output for a given seed is a property of those sixteen lines rather
than of anybody's library.

It costs owning a piece of numerical code, and the burden of proving it is the real SplitMix64
rather than a typo of it.

## Decision

**C.** `GameLogic/Random.h` is SplitMix64, and it is the only source of randomness in the
simulation. Range reduction is plain modulo: the bias is about `bound` parts in 2^64, which for the
handful of small counts the generator draws is not a quantity that exists, and rejection sampling
would make the number of words consumed depend on the values drawn — a worse property for output
that has to be reproducible by inspection.

**The cost of owning it is paid by a pinned test.** `GameLogicTests::RandomTests` asserts the first
three words from seed 0 against the published values (`0xE220A8397B1DCDAF`, `0x6E789E6AA1B965F4`,
`0x06C45D188009454F`). A typo in a mixing constant would otherwise be undetectable — the output
still looks random — and would silently regenerate every galaxy in every stored match.

**The generator draws in a fixed order, and that order is the code's.** Where two values are drawn
for one thing, they go into named locals first rather than into two arguments of one call, because
the order arguments are evaluated in is unspecified in C++ and a sequence that depends on it is not
a sequence.

## Consequences

**What this makes easy.** A seed is a galaxy, on any machine, under any compiler. The mobile port
that `Design/Reference/mobile-portability.md` costs out inherits this for free, alongside the
integer arithmetic R16 already bought.

**What this makes hard.** Nothing yet. If the game ever wants a statistically serious generator —
procedural content, a distribution with a shape — this is not it, and that is a different header
next to this one rather than a change to this one.

**What it costs.** Sixteen lines of numerical code in the tree, and one test whose failure means
somebody mistyped a hexadecimal constant.

**What it forecloses.** Nothing. `<random>`'s engines remain available anywhere outside `GameLogic`,
where reproducibility is not load-bearing.

## What this changes elsewhere

- **AGENTS.md:** no rule changes. R16's determinism argument is what this serves; the ban on
  unordered containers and floating point in `GameLogic` is now enforced by
  `Build/CheckProjectFiles.py`, added in the same commit.
- **Design/:** ADR-004 (the generator is a function of a seed), ADR-012 (the seed is what is
  stored). `Design/Plans/MVP-02-TheLoop.md` slice 1.
- **Code:** `GameLogic/Random.h`, and `GameLogic/Galaxy.cpp` which is its only caller.

## Open questions

Whether the resolver will ever need randomness at all. It should not: the one-pager says combat is
deterministic and that "uncertainty comes from what humans ordered, not from dice". If a rule ever
wants a die, it draws from a stream seeded by the match seed and the tick number, so that a replay
of tick N does not depend on how many draws ticks 1 to N-1 happened to make. Noted here because
that is the design a later slice would need, not because one is planned.
