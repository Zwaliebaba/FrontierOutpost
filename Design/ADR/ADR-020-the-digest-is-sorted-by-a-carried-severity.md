# ADR-020 — The digest is sorted by a severity each event carries

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session for `Design/Plans/4X-01-CoreLoop.md`, Step 4, which names the sort order as a decision and says to record it.
**Supersedes:** —

---

## Context

The digest is the screen. The one-pager is explicit that a player opens the app after work, reads
one list, gives orders and leaves — and that the list is *"one digest per tick, sorted by
consequence"*. `Design/Screens/README.md` shows what that looks like: seven events, the top one
carrying a coloured left border, contact above proposal above economy.

"Sorted by consequence" is a design intent, not an algorithm. Something has to turn it into an
ordering, and the choice has consequences that outlive it, because **the top line is the one a
player acts on**. A digest that ranks the production summary above a capital falling is a digest
that has failed at its only job.

There is a second requirement that constrains the answer. ADR-018 requires that anything iterated
into a result has a defined order. Two events with equal rank must not be able to swap places
between two runs of the same tick, or a replay does not match the tick it is replaying.

## Options considered

### A. Sort by event kind, against a fixed table of kinds

An ordered enum: contact, then loss, then proposal, then economy. Sorting is a comparison of two
enum values.

Simple and immediately legible — the ordering is one list you can read. It fails on the cases that
matter most, because **consequence is not a property of the kind**. Losing a border world and
losing your capital are both `SystemLost`. A proposal that expires this tick and one with three
ticks left are both `ProposalReceived`. The ranking cannot say which of two events of the same kind
comes first, so it falls back on whatever order they were generated in — which is system index, an
ordering with no meaning to a player.

It also forces the enum to carry two jobs. Adding a kind means deciding where in the severity order
it goes, so a purely descriptive addition ("region opened") becomes a ranking decision, and the
enum's order stops being safe to change.

### B. A numeric severity, carried on the event, set where the event is raised

Each `DigestEntry` has a `severity`. The rule that raises the event picks it. Sorting is descending
by that number.

The number is chosen where the context exists. The claims phase knows whether the system that fell
was a capital; the proposal code knows how many ticks are left. A later rule can rank between two
existing ones without renumbering anything, which is why the initial constants are spread out
rather than consecutive.

The cost is that the ranking is no longer in one place. Reading the whole ordering means reading
every site that raises an event — mitigated by naming the constants (`LOST_A_SYSTEM`,
`PROPOSAL_ARRIVED`) in one header rather than writing bare numbers at the call sites, so the
vocabulary is central even though the choice is local.

### C. Rank at read time, from the state, per player

Compute consequence when the digest is shown, by asking the current state how bad each event was.

Rejected, and it is worth saying why clearly: **the digest is history and the state is now.** An
event describing a system you lost at tick 46 would be ranked against whether you hold it at tick
60. Worse, it would make the digest depend on state that is not in the `TickLog`, so a replay of
tick 46 would produce a different order than the player saw. That breaks the whole point of the
log.

### D. Learned or player-configurable ordering

Rejected outright. The one-pager's *What it is not* forbids the surrounding apparatus this would
need, and a player who has to configure their digest has already been given a worse digest.

## Decision

**B. Each `DigestEntry` carries a `severity`; the digest phase sorts descending by it.**

The named values live in `GameLogic/TickLog.h` under `namespace Severity`, spread in steps of fifty
so a later rule can land between two of them:

| | |
|---|---|
| `LOST_A_SYSTEM` | 900 |
| `FIRST_CONTACT` | 800 |
| `UNDER_SIEGE` | 750 |
| `PROPOSAL_ARRIVED` | 600 |
| `PROPOSAL_RESOLVED` | 500 |
| `TOOK_A_SYSTEM` | 450 |
| `LANE_CHANGED` | 400 |
| `ORDER_REFUSED` | 350 |
| `REGION` | 300 |
| `CUSTODIAN` | 250 |
| `ECONOMY` | 100 |

**Losing something outranks gaining something** — `LOST_A_SYSTEM` at 900 against `TOOK_A_SYSTEM` at
450 — and that asymmetry is deliberate. A system you took is a thing you already decided to do and
already know about; a system you lost is news, and it is the one that changes what you do next.

**A refused order ranks above the economy line and below anything about territory.** It has to
appear, because the one-pager's rule that nobody is shown a dead offer as acceptable means a
refusal is information the player is owed. It is not urgent, because nothing happened.

**Ties are broken totally, not left to the sort.** `std::stable_sort` on severity, then on kind,
then on system id, then on lane id. Stability alone would not be enough: it preserves *generation*
order, and generation order is only deterministic because every phase iterates ascending — a total
tie-break makes the digest's order independent of that, so a future phase that raises events in a
different sequence cannot silently reorder anybody's digest (ADR-018).

## Consequences

**The severity is part of the state a replay reproduces**, since it is written into the `TickLog`
when the event is raised. Changing a constant changes the order of digests generated after the
change and not before, which is correct: the player saw what they saw.

**The client does not sort.** It renders the list in the order it was given. That keeps the ranking
a server-side rule, which is where every other rule is (AGENTS.md §2), and means two clients
showing the same digest cannot disagree about what matters.

**Every new rule that raises an event has to pick a number.** That is the cost of option B and it
is the point: a rule author is asked, once, how much this should change what the player does next.
A rule that cannot answer is usually a rule raising an event nobody needed.

**The table will be wrong somewhere.** These are initial values, like everything else in
`MatchRules`, and Phase 0 of the test plan is where a real player says "I didn't notice I was under
siege". The fix is to move a number, not to change the mechanism.

## What this changes elsewhere

`Design/Screens/README.md` shows the reference digest in an order that this ranking reproduces —
contact above proposal above loss above custodian above region above economy — with one difference:
the reference puts a lane cancellation ("system lost") third, and this ranking puts a system loss
above a first contact. The reference is a drawing of one plausible tick, not a specification of the
order, and where they disagree this ADR is the rule.

`FrontierOutpost/MatchState.h` has a seven-value `EventKind` that selects a dot colour. It is not
this enum and should not become it: `DigestKind` names what happened and has fifteen values,
`EventKind` names how it looks. The mapping between them is part of Step 8's client wiring.

## Open questions

**Whether severity should be per player rather than per event.** The same capture is a loss to one
player at 900 and a gain to another at 450, which is handled today by raising two entries with
different severities. That works and is explicit; it would stop working if an event ever had to
reach three players with three different weights.

**How many events one digest may hold.** Nothing truncates it. The reference shows seven and the
rail has room for about a dozen; a tick in which a player loses six systems will produce more. The
sort at least guarantees that what is cut is what mattered least.
