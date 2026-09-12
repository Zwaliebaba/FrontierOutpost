# ADR-054 — A local game does not outlive its window

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision: *"for the local game when I exit the game you always need to remove the current state, so a restart of the game means a complete new local game. This counts only for the local game."*
**Supersedes:** — (narrows ADR-042)

---

## Context

ADR-042 made a restarted server resume the match in its store, and it made no distinction between
the two roles that write one (ADR-028): the headless `--serve` runner and the default host-and-play
process. For `--serve` resumption is the point — a match played over three weeks has to survive
the machine it runs on being rebooted. For host-and-play it was a trap: a player who closed the
window to start again found the same tick, the same board and the same refused orders waiting,
with no control on any screen that said "new game", and the only way out was to know the name of
a file beside the executable.

## Decision

**A host-and-play process erases its match store when it exits, and never loads one when it
starts.** A restart is a new local game. `--serve` is unchanged and still resumes; `--join` still
writes nothing.

The erasure is a scope guard in the composition root declared before the hosted server, so it runs
after the server's thread has joined — the file is written at every lock and must not be removed
from under it. A store a previous process left behind (a crash, a kill) is removed at startup for
the same reason: the rule is that a local game starts fresh, not that the previous one was tidy.

## Consequences

- R13 (AGENTS.md §5) is unchanged: a host-and-play process still writes one store and one log,
  and nothing beside the executable is required to start. The instrumentation log is kept — it is
  an event stream across sessions, not the state of a match.
- The `.finished` rename in `LoadStoredMatch` now only ever happens for `--serve`.
- A player who wants a local game that survives a restart runs it as `--serve` and joins it.
