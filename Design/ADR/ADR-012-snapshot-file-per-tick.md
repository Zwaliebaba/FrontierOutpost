# ADR-012 — The server writes one snapshot file per tick, and that is the only data on disk anywhere

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10.
**Supersedes:** — (narrows the "Distribution" row of the `Design/README.md` §1 baseline and R13 to the client, rewritten in place)

---

## Context

A match lasts three weeks. A server process that restarts — for a fix, a reboot, a crash — in the
middle of one must come back with the match where it was, or three weeks of eight people's play
ends at a stack trace. The tree has no persistence of any kind.

Two rules bear on the answer. R13 says the executable ships alone: no assets folder, no data
directory. It was written about the client and has to be reworded to say so, because a server
with no data directory is a server that forgets. R14 says no third-party dependencies, which
rules out an embedded database and leaves the file system.

ADR-004 makes the state a value: the resolver's input and output are whole `MatchState` records,
and the order books that locked are a value too.

## Options considered

### A. Nothing, for Phase 0

A 48-hour compressed match among friends survives a restart badly and that is tolerated. It
buys nothing that a snapshot file does not, and it means Phase 1 — the first phase whose result
matters — is the first time persistence is exercised.

### B. One file per tick: the state after resolution and the order books that locked

Serialized with the field-by-field little-endian discipline of `Protocol.cpp`, named for the tick,
written before the tick's snapshots are sent. Restart reads the highest tick and resumes. The
match directory is also the audit trail: the exact inputs and output of every resolution, from
which any tick can be replayed with the resolver alone.

It costs a serializer for `MatchState` and the order books, a match directory, and the R13
rewording. Estimated, not measured: tens of kilobytes a tick, a few megabytes a match.

### C. An embedded or hosted database

Queries, transactions, and a dependency R14 forbids, for a record that is written once every six
hours and read once at restart.

## Decision

**B.** `FrontierServer.exe` keeps a match directory beside itself, one per match, holding
`Match.bin` (seed, seat count, `Rules`, `TickSchedule`, seat tokens) and `Tick-NNNN.bin` for every
resolved tick, each the serialized `MatchState` after resolution and the `LockedOrders` that
produced it. The **current order books between ticks are also written**, on every accepted edit,
to `Pending.bin`, so a restart between ticks loses no commitment. `Simulation::Save` and `Load`
(ADR-007) produce and consume the bytes; the server owns the files.

The format is the wire format: the same `Write`/`Read` cursors, the same compile-time size
assertions on fixed records, a version field first. A file the running version cannot read is a
refusal with a message, not a crash.

**The client still ships alone.** R13 is reworded to say the client executable, and the server's
match directory is the single named exception in the tree.

## Consequences

**What this makes easy.** Restart. Replay of any tick from its inputs, which is how a Phase 0
report of "my fleet vanished at tick 31" becomes a test. The event log the test plan asks for
lives in the same directory.

**What this makes hard.** The serializer has to cover the whole state and is one more thing that
must change when the state does; the version field is what makes an old file a message.

**What it costs.** Disk that nobody will notice, and one write per accepted order edit.

**What it forecloses.** Nothing. If a hosted server ever wants a database, the file is what it
imports.

## What this changes elsewhere

- **AGENTS.md:** R13 reworded to the client executable, with this exception named.
- **Design/:** `Design/README.md` §1 "Distribution" row. ADR-007 (`Save`/`Load`), ADR-011.
  `Design/Plans/MVP-02-TheLoop.md` step 4.
- **Code:** nothing yet.

## Open questions

Whether a match directory should be compacted after the match — the last tick and the event log
kept, the rest dropped. Not until disk is measured to matter.
