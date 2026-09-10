# ADR-011 — A headless server hosts a match; the client executable also hosts every seat in-process as a development harness

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10.
**Supersedes:** — (replaces the "one executable" row of the `Design/README.md` §1 baseline, rewritten in place)

---

## Context

The baseline said one executable: `FrontierOutpost.exe` starts the client and the authoritative
server in one process, and the server has its own thread from the first commit. That was right for
proving the replication stack, and it cannot host the 4X. Orders are hidden until they lock, which
a process the player owns cannot guarantee; ticks resolve four times a day whether anyone is
looking, which a process that ends when the window closes cannot do; and six to twelve humans
on six to twelve machines need something to connect to.

The test plan needs both shapes. Phase 0 is six friends against a server that runs unattended for
forty-eight hours. Every build session before that needs to drive a whole match — every seat, every
phase — on one machine with no network and no waiting for a schedule.

## Options considered

### A. Keep one process only

Hidden orders are hidden by convention, nobody's orders resolve while the window is closed, and
Phase 1 cannot run on it. A hot-seat build, at best.

### B. A headless server, and a client that only connects to it

`FrontierServer.exe` hosts the match; `FrontierOutpost.exe` connects over ADR-006's socket. The
loopback and the in-process session go. Clean, and every end-to-end test needs a server process
started first, which is friction in exactly the sessions that most need to run the loop end to
end.

### C. A headless server, and a client that can also host every seat in-process

Both. `FrontierServer.exe` is B's server. `FrontierOutpost.exe` connects to one, or — as a
development harness — starts a `Session` on its own thread over the existing `LoopbackTransport`,
takes every seat, and lets the one client cycle through them and resolve ticks on demand.

## Decision

**C.** Two executables.

**`FrontierServer.exe`** is a new project, `FrontierServer/`, referencing `NeuronCore`,
`NeuronServer` and `GameLogic`. It creates a match from a seed, a seat count, a `Rules` record and
a `TickSchedule` (ADR-007), prints the seat tokens (ADR-016), listens on ADR-006's socket, and
runs until the match is over. It has no window and draws nothing. Its state lives in a match
directory of per-tick snapshots (ADR-012), which is how it survives a restart.

**`FrontierOutpost.exe`** is the client. Given a server address and a seat token it connects.
Given neither, it is the **harness**: it hosts a `Session` in-process over `LoopbackTransport`,
holds every seat's token, shows a seat selector, and exposes `ResolveNow()` as a tap target
labelled as what it is. The harness is a development tool and the ADR says so; nothing about the
game is hidden inside it, and it is not a mode a player is offered.

**The client never links `GameLogic` in a client-side file.** The harness constructs the
`Frontier::World` in the executable's composition root and hands it to the session — exactly the
one place `AGENTS.md` §2 already allows — and no file that draws or handles input includes a
`GameLogic` header. That line is what keeps the harness from becoming a client that knows the
rules.

## Consequences

**What this makes easy.** Every build session runs a whole match on one machine. Phase 0 runs on
a server nobody has to babysit. The same `Session`, `Simulation` and wire records serve both, so
the harness is a test of the server rather than a second implementation of it.

**What this makes hard.** A tenth project, with its own `.vcxproj` and `.filters`, its own row in
the `.slnx`, and the checkers extended to know about it. And a second place things can drift: a
message the harness handles and the server does not is a bug that only appears in Phase 0.

**What it costs.** Keeping the loopback alive, which under B would have been deleted.

**What it forecloses.** Nothing. If the harness ever stops earning its keep, B is a deletion.

## What this changes elsewhere

- **AGENTS.md:** §2's repository map and dependency graph gain `FrontierServer/`; the intro
  paragraph no longer says one executable; R13 becomes a client rule (ADR-012).
- **Design/:** `Design/README.md` §1's "Shape" row. ADR-012, ADR-016.
  `Design/Plans/MVP-02-TheLoop.md` steps 4, 5 and 6.
- **Code:** nothing yet. `FrontierServer/` does not exist; `FrontierOutpost.cpp` hosts one seat
  with no selector.

## Open questions

Where `FrontierServer.exe` runs for Phase 1 and who operates it. A machine, an address and a
person. Not a code question, and not answered here.
