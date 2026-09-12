# ADR-042 — A restarted server resumes the match it stored, on UTC time

**Status:** Accepted â€” narrowed by ADR-054: resumption is `--serve`'s; a host-and-play process erases its store on exit

**Date:** 2026-09-12
**Decided by:** Owner decision, 2026-09-12, on the first finding of the codebase review
(`Design/Archive/2026-09-12-codebase-review.md`): "wire real resumption".
**Supersedes:** — (closes open questions in ADR-024, ADR-026 and ADR-036)

---

## Context

ADR-024 decided that a server writes one match store, that a match is loaded by re-resolving its
orders from tick zero, and that the replayed hash is asserted against the stored one. ADR-026
decided that the schedule is arithmetic over a UTC instant and that a server which slept through
locks owes every one of them. `4X-02` §1 stated the result as done: *"A server restarted mid-match
resumes it, and says so."*

The review found that the tree did not do this. The library half was complete and tested: the
store loaded, the session replayed and checked, the simulation rebuilt from its configuration. The
executable never called any of it. Every launch was a new match, and the store written four times a
day was never read.

Two smaller facts made the gap wider than a missing call. The instant handed to the schedule was
seconds since *process start* from a monotonic clock, with the schedule constructed at zero, so the
"fixed UTC times" of the one-pager did not exist and a restarted server could not have known which
locks it missed even with the store loaded. And nothing persisted the schedule's start instant, or
the seat tokens, so a resumed match would have had no clock and no seats.

## Options considered

### A. Leave it as a Phase 0 simplification and correct the design record

Restart means a new match. Honest, cheap, and the plan's claim is deleted rather than met. It fails
the moment a host closes their window during a forty-eight-hour weekend, which `4X-02` §3 already
lists as the thing to "say out loud" to six people.

### B. Wire resumption on a process-relative clock

Load the store and replay, but keep the monotonic instant. The match resumes at the tick it
reached, and then locks at *restart time plus one interval* rather than on the schedule the players
were told. Missed locks are not owed. Every restart moves the lock times of the day.

### C. Wire resumption on UTC time, and store the schedule and the seats with the match

The instant is UTC seconds since the epoch, from the system clock, read at the one place the
server side is allowed to read a clock (ADR-026). The store carries the schedule's start instant
and interval, the seat tokens, and whether the match has finished. The composition root loads the
store before it builds a server, and either resumes or starts fresh.

## Decision

**C.**

**The instant is UTC.** `HostedServer` derives it from `std::chrono::system_clock` in seconds since
the epoch, and every schedule is anchored on the instant the match began. `TickSchedule` was always
defined over this and nothing changes in it.

**The store is version 2** and carries, beside the configuration, the hash and the turns: the start
instant, the interval in seconds, a finished flag, and the seat tokens. The schedule and the tokens
are server-side facts and the game never sees them, so this widens nothing across the seam
(ADR-025). The version bump means a version 1 store is refused as *not a store*; there was no
version 1 store worth keeping, because nothing ever read one.

**Loading is `Session::Resume`**: replay through `Reload`, refuse with null when the hash does not
reproduce, otherwise a session on the stored schedule with the stored tokens and the stored turns
kept, so the next lock is appended to the history rather than starting one.

**The composition root loads or creates**, in both roles, before it builds a server. Three answers:

- **No store**: a new match. In host-and-play the lobby opens and the seats screen runs; in
  `--serve` the fixed Phase 0 tokens are used, as before.
- **A store that is not a store, or is cut short**: fatal, with the reason, and the file is left
  where it is. ADR-024 asked for exactly this — a corrupt store must fail loudly rather than start
  an empty match on top of a real one.
- **A store whose match has finished**: it is the record of that match, not something to resume
  into. It is moved aside as `lockstep-match.store.finished` and a new match starts. This answers
  ADR-024's third open question, *how a match ends its file*: it ends by being renamed once, the
  next time a server starts, and the previous finished record is replaced.

**A resumed match skips the seats screen.** Its seats were chosen when it began; the tokens are in
the store and the same people are admitted to the same seats, which closes ADR-036's first open
question. The host takes the first stored seat unless `--token` names another. The command line's
`--phase0` and `--tick` are ignored on resume: the rules are in the store and the match was played
under them.

**A replay that does not reproduce the hash is fatal on the server thread**, reported through
`HostedServer::Failed()`, written to the match log, and the headless role exits with a failing
code. It is not a warning and there is no "resume anyway": the simulation has changed under a live
match, and the only honest thing to do is stop and say so.

## Consequences

**A restarted server owes exactly the locks it slept through.** The stored start instant and the
current UTC time say how many locks have passed; the stored turns say how many were resolved;
`LocksOwed` is the difference. Five days offline is twenty ticks resolved at once, as ADR-026
intended and the session tests already showed in a loop.

**Lock times are fixed times of day.** A match begun at 06:00 UTC on a six-hour tick locks at
06:00, 12:00, 18:00 and 00:00 through every restart, which is the one-pager's cadence and was
previously true of no build.

**A finished match leaves one file behind**, replaced by the next finished match. That is the
minimum record the test plan needs and the minimum disk it can cost; a server that hosts many
matches in sequence keeps only the last, which is a limit to know about rather than a defect.

**A clock that jumps backwards** still stalls the match until real time catches up, as ADR-026
recorded. A system clock can be set; a monotonic one cannot, so this decision trades the
impossibility of that failure for the possibility of resumption. The session tests are unaffected
because they drive explicit instants.

**The store now carries tokens in the clear**, which ADR-029 already accepts for the wire. The file
sits beside the executable on the host's machine, which is where the seats screen shows them anyway.

## What this changes elsewhere

- **Code:** `NeuronServer/MatchStore` (version 2), `NeuronServer/Session` (`Resume`, tokens),
  `Lockstep/HostedServer` (UTC instant, a resume constructor), `Lockstep/Lockstep.cpp`
  (load-or-create in both roles). `NeuronServerTests` covers the round trip, a resume that
  continues the schedule and keeps the history, a resume that refuses a tampered store, and the
  finished flag.
- **Design/:** ADR-024, ADR-026 and ADR-036 gain a status note pointing here. `4X-02` §1's claim is
  now true and §6's sixth item is amended.
- **AGENTS.md:** R13's server exception names the `.finished` rename beside the store.

## Open questions

**Whether a host should be able to choose to start fresh while a live store exists.** Today the
answer is to move the file aside by hand. A `--new` flag would be one line and one more thing to
document; nobody has asked for it.

**What a reconnecting player is owed beyond the current snapshot**, which ADR-028 left open, is
still open: a resumed server sends the current state on `Hello` and nothing it missed.
