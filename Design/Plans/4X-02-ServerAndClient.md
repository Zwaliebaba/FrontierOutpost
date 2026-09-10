# 4X-02 — The server, the client and the network

**Status:** Stub, deliberately. Written 2026-09-10 when `4X-01` was split; **do not fill this in
until `4X-01` lands.**

`4X-01` builds the loop headless: a galaxy, a resolver, every rule in the one-pager's build order,
and scripted players proving it end to end in one process. This plan takes that and puts it behind
a server, in front of the main page, and on a network, so that the test plan's Phase 0 can run —
six people, a one-hour tick, a forty-eight-hour match.

## Why this is a stub and not a plan

The steps below were written as part of `4X-01` and were specified against things that do not exist
yet: Step 11 wired the client to a snapshot format Step 8 had not designed, and Step 12 chose a
framing before anyone knew how large a message was. That detail was going to be rewritten, and
`Design/README.md` §2 is explicit that a Plan changes as it meets reality. Writing it now buys a
tidier document and a worse one.

What is kept below is the *intent* of each step and the ADRs each owes, because those do not depend
on the shapes. When `4X-01` closes, its close-out step writes this plan properly.

## The ADRs this plan owes

- **Persistence, and what R13 means for a server.** *(Owner decision; blocks the first step.)* A
  three-week match must survive the server process. Options: (a) the server writes to a file beside
  itself and R13 is read as applying to the client, which still ships alone; (b) memory only, and a
  restart loses the match — acceptable for Phase 0's 48 hours and nothing after; (c) a database,
  which R14 forbids. *Recommendation:* (a), and the file is **the seed plus every locked order
  set** rather than a snapshot, which makes it the same decision as the next one.
- **Persist and replay by re-resolving.** Loading a match is re-running it from tick zero. At four
  ticks a day for twenty-one days that is 84 resolutions, each a few milliseconds — `4X-01` Step 9
  measures the real figure. It buys a tiny file, gives *Replay tick N* for free, and turns a
  determinism bug into a load that visibly diverges. It costs: the resolver can never change an
  in-flight match's history without a migration. It is the payoff ADR-018 exists to make possible.
- **The seam.** ADR-007's `Simulation` was `ApplyOrder(MoveToOrder) / Tick() / Snapshot()`. The 4X
  needs an order *set* per player in, and a per-player snapshot and digest out, because players see
  different things. ADR-007's argument for an abstract class in `NeuronCore` — the dependency graph
  forbids the edge, and the test suite gets a counting fake — still applies.
- **Time and the schedule.** The interval and the phase of day are match parameters; a
  `TickSchedule` is pure arithmetic and the clock is injected, so tests drive it and the simulation
  never sees one. Decide what happens to a lock the server slept through.
- **Transport.** ADR-006's queues were 20 Hz in one process. This game is four messages a day per
  player and cares about reliability, not latency, so TCP with length-prefixed frames is the
  obvious answer. `NeuronCore.h` already includes WinSock2 and links `ws2_32`.
- **Identity for Phase 0.** A per-match, per-player token, issued by whoever starts the server and
  typed once into the client. Honest about being nothing more. Log every login: the login curve is
  the test plan's primary instrument.

## The steps, in intent

1. **The seam, the session, the schedule, the file.** `NeuronServer::Session` owns a simulation,
   accepts order sets from identified players, marks presence, and at each lock resolves,
   persists, and publishes new snapshots and digests. Tested in `NeuronServerTests` against a
   counting fake, as ADR-007's did.
2. **The fixture retires.** `FrontierOutpost` decodes a snapshot into `MatchState`, sends the
   orders rail's edits as an `OrderSet`, takes the countdown from the server's next lock, and
   points *Replay tick N* at the server's tick log instead of the stub. In-process first, so it is
   verifiable before the network exists. `MatchFixture` becomes a test fixture, which is what its
   header always said would happen.
3. **Six machines.** Transport and identity. The server logs every event the test plan lists,
   timestamped UTC — a Phase 0 that cannot draw the login curve answers nothing.
4. **Close out.** Archive both plans with a *What shipped* section, update AGENTS.md §2's
   repository map, and report what Phase 0 needs that neither plan built.

## What the original draft said, kept for the session that writes this properly

The text below is the `4X-01` draft as it stood before the split. It is **not** current and its
specifics are expected to be wrong; it is here so the detail is not lost and so the next author can
see what was already thought through.

---

### (draft) Stage B — the server shell

#### (draft) Step 10 — The seam, the session, the schedule, the file

`NeuronCore/Simulation.h` returns, with the 4X signature from the seam ADR. `NeuronServer/Session`
owns one, accepts order sets from identified players, marks presence, and at each lock — driven by
`TickSchedule` and the injected clock — resolves, persists, and makes the new snapshots and digests
available. Persistence is the seed and the ordered locked order sets; loading a match is
re-resolving it, and the session asserts the replayed hash against the last live one. A lock the
server slept through is resolved on wake, once per missed tick, in order, with the orders it has.

**Tests (`NeuronServerTests`):** `TickSchedule` arithmetic against fixed instants, including a
one-hour and a six-hour interval and the day boundary; a `CountingSimulation` proves the session
resolves once per lock and never between; a session saved, discarded and loaded produces the same
hash; two missed locks produce two resolutions in order; presence marked by a connection between
locks and not by one after.

### (draft) Stage C — the client, wired

#### (draft) Step 11 — The fixture retires

`FrontierOutpost`: decode a snapshot into `MatchState`; send the orders rail's edits as an
`OrderSet` at any time before lock (the server keeps the latest); take the countdown from the
server's next lock instant rather than a local number; on lock, flip the rail, take the new digest
and snapshot, advance *Replay tick N*; the replay panel steps through the server's `TickLog`
rather than the stub's phase list. The in-process case first — session in the same executable, as
MVP-01 did — so Stage C is verifiable before Stage D exists. `MatchFixture` becomes a test fixture
and stops being the boot path, which is what its header always said would happen.

**Tests (`NeuronClientTests`):** snapshot to `MatchState` decode against a hand-built snapshot;
an edited orders rail serialises to the `OrderSet` the server expects. And run it: the main page
against a generated galaxy, all seven interactions from ADR-014 still working.

### (draft) Stage D — the network, and Phase 0

#### (draft) Step 12 — Six machines

The transport and identity ADRs. TCP, length-prefixed frames, the server listens, each client
holds one connection and a token. The server logs every event the test plan lists, timestamped
UTC. A `--serve` flag or a second executable; the choice is part of the transport ADR — note that a
second executable changes AGENTS.md §2's "nine projects" and that R13 applies to the client.

**Tests:** framing round-trip; a client reconnecting after the server restarted gets the current
snapshot; a token that is not on the match's list is refused. And the real test: two machines,
one match, one tick.

#### (draft) Step 13 — Close out

Move this plan to `Design/Archive/` with a *What shipped* section above it, as MVP-01 has. Update
AGENTS.md §2's repository map — `GameLogic` and `NeuronServer` are no longer empty and their rows
say what they hold. Confirm every §3 ADR is written and every §7 row has a step and a test. Report
per Design/README.md §6, including the per-tick resolution time from Step 9 and what Phase 0 needs
that this plan did not build.

---

