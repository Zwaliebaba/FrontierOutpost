# ADR-026 — The schedule is arithmetic, and a sleeping server owes every lock it missed

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Build session for `Design/Plans/4X-02-ServerAndClient.md`, Step 1.
**Supersedes:** —

---

## Context

The one-pager fixes the cadence: *"Four ticks a day at fixed UTC times."* The test plan needs it
adjustable — Phase 0 runs a one-hour tick over forty-eight hours, Phase 1 a six-hour tick over
fourteen days — which is why `MatchRules::tickIntervalSeconds` is a parameter and not a constant.

Two questions follow, and the second is the one with a wrong answer that looks right.

**Where does time enter?** R16 forbids the simulation a wall clock, and ADR-018 makes that
load-bearing rather than stylistic: a match is a pure function of its seed and its orders, and a
clock reading inside it would be a hidden input. But *something* has to know what time it is, or no
lock ever happens.

**What does a server owe when it was not running at a lock?** A deploy, a crash, a restart, a laptop
lid. This will happen during a three-week match, probably several times.

## Options considered

### Where time enters

**A. The session reads a clock.** Simplest, and it makes the session untestable in the way that
matters: a three-week match cannot be driven in a loop, so the interesting cases — two missed locks,
a match ending mid-catch-up — can only be reached by waiting or by faking the system clock.

**B. The session is told the instant.** `Advance(now)`. Time enters at the one call site that has
it, everything below is arithmetic, and a test drives eighty-four ticks in a `for` loop.

**C. An injected clock interface.** `IClock::Now()`. The same testability as B with an extra
indirection, a virtual call and a fake to write. It buys the ability to have the session *decide*
when to look, which it does not need — something already owns the event loop.

### What a sleeping server owes

**D. Resolve once, jump the tick forward.** The server wakes, sees it is three locks behind,
resolves once and sets the tick to where the clock says it should be.

This is wrong and it is wrong in a way that would not be noticed for weeks. ADR-024 stores a match
as *its orders, one entry per tick*, and loads it by replaying them. A tick that was skipped leaves
no entry — so the stored match has three ticks where the live one had four, and the replay produces
a different state. The hash check would catch it at the next restart, reporting a determinism
failure, with the actual cause three days upstream.

**E. Resolve every missed lock, in order, with the orders it has.** The match has a hole-free order
list. Players who submitted before the server went down have their orders resolved; players who did
not are absent for those ticks, which is exactly what happened.

## Decision

**B and E.**

`Neuron::TickSchedule` is pure arithmetic over an `Instant` — UTC seconds since the epoch, as a
plain `std::int64_t`. It holds no clock and never reads one. `Session::Advance(now)` is where time
enters the server, and it is the only place.

Locks are at `startedAt + n × intervalSeconds`. **The phase of day falls out of `startedAt`** rather
than being a separate field: a match started at 06:00 UTC on a six-hour tick locks at 06:00, 12:00,
18:00 and 00:00, which is the one-pager's fixed UTC times with no extra machinery.

**Tick 0 locks one interval after the match starts**, because tick 0 is the state before anything
was played and the first lock is what produces tick 1.

**`LocksOwed` returns a count, not a flag**, and `Advance` loops on it. That signature is the
decision: a boolean `IsLockDue` would have made option D the natural implementation and the bug
invisible.

**Three guards against arithmetic that would otherwise go quietly wrong:**

- An interval of zero is floored to one in the constructor. Zero would owe infinite locks at the
  first instant after the match started.
- An instant before the match started owes **zero** locks, not a negative count wrapped to
  enormous — `LocksDueAt` compares before it subtracts.
- The countdown clamps at zero rather than going negative once a lock has passed.

**A finished match stops resolving, and the session is what knows that.** The schedule is arithmetic
and will happily keep producing locks forever; `Advance` checks `IsFinished()` before each one. This
is the question ADR-023 left open — *whether the match should stop resolving when it ends* — and
this is the answer: in the session, because that is the layer that has both the clock and the
verdict.

## Consequences

**A test can drive a whole match in a loop**, which is what `SessionTests` does — including five
days offline caught up in one call.

**The countdown is server-authoritative.** `SecondsUntilNextLock` comes from the schedule, so six
clients agree about when the tick is instead of each running its own timer and drifting. The client
currently counts down a local number (`MatchFixture`); Step 2 replaces it with this.

**A long outage produces a burst of resolutions and a burst of digests.** Twenty ticks of catch-up
is twenty digests for every player, arriving at once. That is honest — those ticks did happen — but
it is a poor experience, and the client will need to say so rather than showing twenty notifications
it has no concept of.

**Nothing here handles a clock that goes backwards.** `LocksOwed` returns zero, so the session
simply does not resolve until real time catches up. That is the safe failure and it is not the
*correct* one; see below.

## What this changes elsewhere

`NeuronCore` gains `TickSchedule.h`. Nothing in `GameLogic` changes — which is the point: the
simulation still has no idea what time it is.

## Open questions

**A clock that jumps backwards** — an NTP correction, a VM restored from a snapshot — stalls the
match until real time catches up. For a correction of seconds this is invisible; for one of hours it
is a match that appears frozen with no explanation. The session could detect a backwards jump and
say so; it does not.

**Whether missed locks should be resolved at full speed or paced.** Twenty resolutions take under a
millisecond and twenty digests take a player ten minutes to read. The server has no reason to pace
them; the client may have every reason to.

**What the schedule should do about a match that starts in the future.** It owes zero locks, which
is right, and nothing distinguishes "not started yet" from "started and nothing due", which may
matter when a lobby exists.
