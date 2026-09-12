# Tick resolution cost — what a tick costs, and what a whole match costs to replay

**What this is.** A Reference (`Design/README.md` §2): a measurement, not a decision. It exists
because `4X-01-CoreLoop.md` §3 recommends persisting a match as a seed and a list of order sets,
reloaded by **re-resolving from tick zero** — and that recommendation is only sound if re-resolving
is cheap. This is the number it rests on.

**Measured** on 2026-09-11 by `ScriptedMatchTests::ATickResolvesFastEnoughToReplayAWholeMatch`,
which plays a complete 84-tick match with six scripted policies and times only the call to
`TickResolver::Resolve` — not the bots, not the snapshots they read, not the invariant checks.
`std::chrono::steady_clock`, summed across the match and divided by the ticks. One machine, one run
per configuration, x64.

**Not measured, and not claimed:** anything about a server. When this was measured there was no
server; there is one now (`4X-02`, ADR-028, 2026-09-11) and it has not been measured either — there
is still no figure here for wire time, disk time, wake-up latency or how many matches one process
could hold. Those are the numbers that would actually size a deployment and none of them exist yet.

---

## The numbers

| Configuration | Per tick | Whole 84-tick match |
|---|---|---|
| Debug | 1.031 ms | 86.6 ms |
| Release | 0.027 ms | 2.2 ms |

The match is six players on a 31-system galaxy: 28 of the 31 systems claimed by the end, 27 with
buildings on them, four trade lanes open at the peak, and a fight somewhere most ticks. It is a
busy match rather than a quiet one, which is the right thing to measure.

The Debug figure is roughly **forty times** the Release one. That ratio is what an unoptimised build
with iterator debugging costs on a workload made almost entirely of small vector walks, and it is
worth writing down because CI builds everything in Debug and only `GameLogicTests` in Release
(ADR-048) — a future session reading a CI timing and comparing it to this table needs to know which
column it is in.

## What it means for persistence

Replaying a whole match from its seed and its order sets costs **about two milliseconds** in
Release. A three-week match, reconstructed from nothing, in less time than a frame.

That removes the argument against `4X-01` §3's recommendation. A server does not need to store
match state at all: a seed, a rules struct and the order sets are enough, and they are *small* —
an `OrderSet` is ids, enums, counts and flags with no free text (`NoOrderCarriesFreeText`), so a
whole match's orders are a few kilobytes rather than a snapshot per tick.

**It does not decide the question**, which is an owner decision and belongs to `4X-02`: R13 says the
executable ships alone with no runtime file dependency, and a match that survives a restart has to
live *somewhere*. What this measurement settles is only that the cheap option is not too slow — the
collision with R13 is unaffected. *The owner took it on 2026-09-11: a match is its seed and its
orders, written beside the executable by a process acting as the server, and R13 was amended to
allow that one file (ADR-024). A restarted `--serve` process reloads it by exactly this replay
(ADR-042).*

## What would change these numbers

**Player count.** Twelve players means 61 systems rather than 31, and the resolver's costliest
loops are per system and per fleet. Not measured; a rough expectation is a little over double, not
an order of magnitude, because nothing here is quadratic in players.

**Match length.** Linear. The per-tick figure is what matters.

**Anything that makes a phase super-linear.** Today every phase is a walk over players, systems,
fleets or lanes, and the only nested walk is combat's melee, which is bounded by the number of
sides at one system. A future rule that compares every fleet to every other fleet would change the
shape of this table and should be measured again when it lands.

## Reproducing it

```
msbuild Lockstep.slnx /t:Rebuild /p:Configuration=Release /p:Platform=x64
vstest.console.exe x64\Release\GameLogicTests.dll /Platform:x64 ^
  /Tests:ATickResolvesFastEnoughToReplayAWholeMatch /logger:"console;verbosity=detailed"
```

The test prints the line this table was read from. It also asserts loose bounds — 50 ms a tick, two
seconds a match — which exist to fail if a tick ever becomes expensive enough that re-resolving
stops being a reasonable way to load a match. They are deliberately nowhere near the measured
values: a tight bound on a laptop timing is a test that fails on somebody else's machine for no
reason.
