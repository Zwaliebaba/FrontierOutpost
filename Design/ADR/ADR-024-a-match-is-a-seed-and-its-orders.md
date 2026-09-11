# ADR-024 — A match is a seed and its orders, and the server may write one file

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** **Owner decision, 2026-09-11**, choosing between the three options offered at the close of `4X-01`. This is the decision `4X-02-ServerAndClient.md` §2 names as blocking its first step.
**Supersedes:** —

---

## Context

A match runs for three weeks at four ticks a day. The server process will not. It will be restarted
for a deploy, a patch, a crash or a power cut, and the match has to still be there afterwards — a
Phase 0 that loses a forty-eight-hour match on day two has answered nothing and has also wasted six
people's time, which is the scarcer resource.

Against that, `AGENTS.md` **R13**: *the executable ships alone. No assets folder, no data directory.*
It is one of the tree's load-bearing rules and it has shaped real decisions — the font is embedded,
the colour table is embedded, the shaders are compiled into headers, and the sixty-four system names
in `GalaxyGenerator.cpp` are a `constexpr` array specifically because there is no name file to load.

R13 was written for **assets**: things the program reads to draw itself. A match store is not an
asset. But the rule as written does not say that, and a rule the tree visibly breaks is a rule
nobody believes the next time it is cited — which is the real cost of leaving this unresolved.

`4X-01` also changed what the answer costs. The resolver is a pure function of `(rules, seed,
orders)` (ADR-018), and `Design/Reference/tick-resolution-cost.md` measures a whole 84-tick match
replaying in **2.2 ms** in Release. That number did not exist when the question was first raised and
it is what makes the cheapest option also the best one.

## Options considered

### A. The server writes one file; R13 is read as binding the client

The server keeps a match store beside itself. The client still ships alone and still reads nothing.
R13 gains a sentence saying which half it binds.

Costs a documented amendment to a rule that has been absolute. Buys a match that survives the thing
that will certainly happen to it.

### B. Memory only

R13 stays untouched and absolute. A restart loses the match.

Honest and cheap, and survivable for exactly one Phase 0 of forty-eight hours. It fails the moment
Phase 1 runs a three-week match at a six-hour tick, which is the next thing after Phase 0 — so it
buys a rule's purity at the price of building the same thing again in a month.

### C. A database

Rejected before it was offered: **R14** says the build depends on the Windows SDK and the MSVC
standard library and nothing else. A database is a third-party dependency by any reading.

### D. Build behind an interface and decide later

A store interface with an in-memory implementation, the file arriving when Phase 0 proves it
necessary. Defers the amendment at the cost of an abstraction that would very likely only ever have
one real implementation — and deferring is how a rule collision becomes something the code decided
by accident.

## Decision

**A, with the store being the seed and the orders rather than the state.**

**What is written.** A match file is the `MatchRules`, the seed, and every locked `OrderSet` in tick
order. Nothing else. No snapshot, no `Match`, no `TickLog`.

**How it is loaded.** By **re-resolving from tick zero**. The session replays the order sets through
`TickResolver::Resolve` and asserts the resulting hash against the one it last wrote. That assertion
is not a debug aid and is not optional: it is the entire safety argument for this design, and a
mismatch means the simulation has changed under a live match.

**Why the orders and not the state.** Four reasons, and the last one is the one that decided it:

- The file is tiny. An `OrderSet` is ids, enums, counts and flags with **no free text**
  (`NoOrderCarriesFreeText`), so a full match's orders are a few kilobytes rather than a snapshot per
  tick.
- *Replay tick N* — which the main page already has a button for — comes for free, because replaying
  is what loading already does.
- A determinism bug becomes a **load that visibly diverges** rather than a corruption that does not.
  The hash check catches it at startup, once, loudly.
- It is the payoff ADR-018 was written to make possible. That ADR pinned the PRNG by value, banned
  the wall clock and forced every iteration into a defined order specifically so that this would be
  available later. Declining it now would mean having paid the price and left the goods.

**R13 is amended in this commit**, not when the server is built. The rule now says it binds the
shipped client and names the server's match store as the one sanctioned exception. Amending it later
would mean a period during which the plan of record contradicts the conformance record, and whichever
one a future session read first would be the one it believed.

## Consequences

**The resolver can never change an in-flight match's history without a migration.** This is the real
cost and it is not small. A rule fix during Phase 0 either ends the running match or accepts that
its replay no longer reproduces it. There is no third option, and the ADR says so here rather than
letting a future session discover it at the worst moment.

**The hash check turns a silent class of bug into a loud one.** Any accidental non-determinism —
an unordered iteration, a `float`, a clock — surfaces as a match that will not load, which is
dramatically better than one that loads into a different world than the players left.

**R13 is weaker than it was**, and deliberately. The next thing that wants to write a file will cite
this ADR. The amendment names *the match store* specifically rather than granting the server
filesystem access generally, so that the next request is a new decision rather than an inference.

**Nothing about the client changes.** It ships alone, reads nothing, and still embeds its font, its
colours and its shaders.

## What this changes elsewhere

`AGENTS.md` R13, in this commit. `4X-02-ServerAndClient.md` §2 loses its blocking owner decision and
its first step can start.

## Open questions

**Where the file lives, and what happens when it is missing or corrupt.** Beside the executable is
the obvious answer and the ADR deliberately does not fix it, because it interacts with how a server
is deployed and nobody has deployed one. A corrupt or truncated store must fail to load loudly
rather than start an empty match on top of a real one.

**Whether one file holds one match.** Phase 0 needs one match. Several matches in one process is a
`4X-02` question and this decision does not prejudge it.

**How a match ends its file.** A finished match's store is a complete record of everything that
happened, which is exactly what the test plan wants to read afterwards — and also a thing that
accumulates on a disk nobody is watching.
