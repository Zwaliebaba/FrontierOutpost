# ADR-030 — The game narrates and the server writes it down

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Build session for the Phase 0 preparation, following `Design/Plans/4X-02-ServerAndClient.md`.
**Supersedes:** —

---

## Context

The test plan opens with a section that is not about the game at all:

> *"Instrumentation (all phases): timestamped events for login, session start/end, order edit, order
> lock, tick resolution... The login curve is the primary instrument; surveys are secondary."*

Everything Phase 0 is supposed to answer is answered from that list. Whether the trade-lane mechanic
is used, whether people give orders after their capital falls, whether anyone comes back on day two
— all of it is a question about a timestamped event stream, and none of it is a question you can ask
a running process.

Two things stood between that list and a file.

**The events did not exist.** ADR-025 put the game behind a byte seam so that `NeuronServer` never
names a `GameLogic` type, which is the right boundary and also means the server cannot see that a
capital fell. It closed by naming the problem and the answer: *"either the session learns to read a
digest, or the game emits an instrumentation stream beside it. The second is cleaner and is the one
to take."* This ADR takes it.

**Nothing could write a file.** R13 says the executable ships alone, and it has exactly one
sanctioned exception — the match store of ADR-024. R13 also says, in its own words, that *"the next
thing that wants to write a file is a new decision, not an inference from this one."* A log is the
next thing. So this ADR exists because the rule demanded one, which is the rule working.

## Options considered

### A. Leave it in the debug output

What `MatchServer::TakeLog` did: the events reach `DebugTrace`, and a debugger sees them. Free, and
useless — `--serve` on a machine in a cupboard for forty-eight hours has no debugger attached, and
the output is gone when the process ends. It fails the only requirement.

### B. The session reads the digest

The digest already contains the proposal, lane and combat events. The session could decode one and
log what it finds.

It breaks ADR-025 on the first line: the session would have to know the digest's shape, which is a
`GameLogic` type, and the seam stops being a seam. It is also the wrong information — a digest says
what a *player* is told, and instrumentation wants what *happened*, which includes things no player
sees (a proposal ignored until it expired) and excludes things every player sees.

### C. The game emits events and the server writes them

A third method on the seam, `TakeEvents()`, returning already-formatted lines. The game knows what
happened and says so in words; the server knows where files go and puts them there. Neither learns
anything about the other.

### D. C, but structured — one record per event with typed fields

Strictly more useful to a program, and its reader is not a program. It also requires guessing the
schema now, before a single match has been played, and a guessed schema is discovered to be wrong
after the runs it was supposed to measure.

## Decision

**C. The game narrates; the server timestamps and writes.**

`Neuron::Simulation` gains `TakeEvents()`, returning `std::vector<std::string>` and clearing.
`MatchSimulation::RecordEvents()` fills it during resolution, from state the resolver already
computed. `Session` forwards it. `NeuronServer/MatchLog` prefixes each line with a UTC timestamp and
appends it to one file.

**Three properties, each of which is a decision rather than an implementation detail:**

**The lines are formatted by the game, not by the server.** `"T12 fleet-order-after-capital-fall
player=3 fell-at=T9 orders=2"` is written where `capitalFellAt` lives. The alternative — a typed
event the server renders — puts a copy of the game's vocabulary on the far side of the seam, which
is exactly what ADR-025 was for.

**The timestamp is applied outside the simulation, and it is UTC.** R16 forbids a wall clock in
`GameLogic`, and this does not breach it: the game's events carry the *tick*, and `MatchLog` adds
the wall time as it writes. That is the one deliberate wall clock in the tree, and it has to be
there, because the login curve is a curve against real time. UTC because six people in four time
zones reading one log is exactly the case it has to survive.

**It is plain text, one line per event.** Its reader is a person with a spreadsheet.

## Consequences

**R13 now has two exceptions, and they are both named.** A process acting as the server may write a
match store and an instrumentation log. A client writes nothing, still. The rule's own sentence about
the next file applies unchanged to the third one.

**The shipped executable still needs nothing beside it.** Both exceptions are files the server
*creates*; neither is a file it requires in order to start. A first run with no store and no log
works, which is what R13 is actually protecting.

**Both paths resolve next to the executable, not to the working directory.** This is the bug that
was in this code before it was in this ADR: `MatchLog` wrote nothing for a full test run because
`"frontier-match.log"` — the file is `lockstep-match.log` since the rename of ADR-035, but it was
not then — was relative to wherever the process was launched from. `BesideTheExecutable`
fixes it, and the reason it is worth a paragraph is that R13's warning about "a working-directory
assumption" turns out to bind the two files the rule allows just as hard as the ones it forbids.

**A log that cannot be opened is reported once and then ignored.** Losing instrumentation is bad;
ending a forty-eight-hour match because a disk filled up is worse.

**Every write reopens the file.** A handle held for three weeks is a file nobody can copy or read
while the match runs, and reading the log mid-match is precisely what somebody watching a Phase 0
run wants to do.

**The events are only as good as what the game chooses to say.** H3's measurement — orders given
after a capital falls — exists because `RecordEvents` emits that line. A hypothesis nobody adds a
line for is a hypothesis Phase 0 silently does not answer, and the failure looks like a clean result.

## What this changes elsewhere

`Simulation` gains one method, and so every implementation of the seam must have something to say.
`MatchServer::TakeLog` still exists and still carries the server's own events — connections, logins,
refusals — which are the server's to know and not the game's. They land in the same file.

ADR-029's rule survives intact: a refused token is never written, to this file or any other.

## Open questions

**Rotation, size and retention.** There is none of any of it. Forty-eight hours at a few lines a
tick is a small file; a three-week match at a real tick rate is still a small file. The first run
that is not will need an answer.

**Whether order edits are observable at all.** The test plan asks for them. The server sees a
replacement order set arrive, and the game sees only the last one before the lock, so today the
count of edits is a fact only the client holds. It is the one item on the instrumentation list that
this ADR does not deliver, and it is recorded here rather than left to be discovered during a run.

**Whether a second match in the same directory should append or start a new file.** It appends. The
`match-start` line separates them, which is enough for a person and not for a script.
