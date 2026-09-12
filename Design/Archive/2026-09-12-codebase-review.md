# Codebase review, 2026-09-12 — answered

**What this is.** A whole-tree review of the C++ (`Design/README.md` §2: a review goes to
`Archive/` when it is answered). The review was read against every file in the five libraries,
the executable, the tests, the checkers, CI and the design record, on a Linux container with no
MSVC. **Nothing was compiled or run under MSVC for it.** The owner answered its four questions the
same day and the answers were implemented on the branch this file was committed on; §3 says which
recommendations were taken and §4 which were not.

**How the code changes were verified.** Not by the tree's own build: MSVC is not available here.
`GameLogic` was compiled with clang 18 and libstdc++ 13 against a shim of the Windows headers, and
the scripted match played to the same hash on every run; `NeuronServer` (minus the socket) and
`HostedServer.cpp` were type-checked the same way, which found two compile errors in the resumption
change before MSVC could. Every edit is formatted (`CheckFormat.py`, clean) and registered
(`CheckProjectFiles.py`, clean apart from the shader path-separator false positives that script
reports on Linux). **The first MSVC build of this branch is the real check**, and the pinned hash
test is the one most worth watching.

---

## 1. Verdict

The architecture is the right shape for this game and unusually well argued. The byte-shaped
`Simulation` seam keeps the server library from naming a game type; the resolver is a pure
function with the state threaded through six copy-returning phases; the store is orders rather
than state; the random generator is pinned by value; every wire decoder assumes a hostile peer and
is fuzzed. The test suites are shaped by that architecture rather than bolted on.

The weakest point was that the property the design advertised hardest, a three-week match that
survives a server restart, did not exist in the tree. Below that were a handful of concrete
defects, a Release-build error path that could not reach its catch, and a house style that had
started to hurt.

## 2. Findings

### Architecture

**Resumption was design fiction.** The library half was complete and tested — the store loaded,
the session replayed and checked the hash, the simulation rebuilt from its configuration — and the
executable never called any of it. The plan for stage C stated restart resumption as done. The
server's clock instant was also seconds since process start from a monotonic clock with the
schedule constructed at zero, so ADR-026's fixed UTC lock times did not exist and nothing persisted
the start instant. *Fixed: ADR-042.*

**The Release error path could not reach its catch.** Every diagnostic went through a `Fatal` that
discarded its formatted arguments and threw a fixed string, and executed a breakpoint instruction
unconditionally, which with no debugger attached raises an exception nothing handles and kills a
headless server before the throw. *Fixed.*

**The dedicated server role was not operable as built.** A Windows-subsystem binary with no
console, log lines to the debugger output, an infinite loop with no exit on failure, and a
hard-coded store name so two matches on one machine overwrite each other's store. *The exit on
failure is fixed; the shared store name remains — see §4.*

**Sockets.** The reuse-address option on Windows is the port-hijack option, not the POSIX one.
*Fixed.* The client's connect blocks on the render thread with no timeout, and resolution is IPv4
only. *Remain — see §4.*

**The test project compiles seven of the executable's files a second time** rather than linking a
client library. ADR-040's argument that a library would change what ships is wrong: a static
library is a link unit, not a runtime file. *Remains — see §4.*

**The game library includes the Windows headers by a uniformity decision**, which gives up the
platform-free simulation the portability reference calls the tree's most valuable property. *Owner
declined to reverse it.* The clang shim build used to verify this branch is the cheap workaround.

**The core umbrella header had a file-scope `using namespace`**, breaking the tree's own R10
through every precompiled header. *Fixed.*

### Defects found by reading

- **Cross-list rejection index.** Validation reported a rejected order as an index within its own
  list with no list discriminator, and the lock skipped any fleet order, build or proposal at that
  index. A refused fleet order at position zero silently dropped a legal build at position zero.
  *Fixed, with a test.*
- **Join countdown.** The welcome carried zero and the state after it was built with an instant of
  zero, so a joining client received the lock's absolute time rather than the remainder and kept
  editing after the server had locked. *Fixed, with a test.*
- **Enums decoded off the wire without range checks** in orders, snapshots, digests and refusals.
  *Fixed, with tests; the client now ignores a state that did not decode completely.*
- **The rules field-count guard compared a constant with itself**, and the constant was 34 for 38
  fields written. *Fixed: one field list drives writer, reader and count, with a size tripwire.*
- **Non-ASCII paths** were narrowed to question marks, so an executable in a folder with a
  diacritic wrote no store and no log. *Fixed: UTF-8 paths, opened wide.*

### Maintainability

**Comments as changelog.** Roughly a fifth of every file was prose and much of it history. *Swept,
and the rule is in AGENTS.md §4 and §7.*

**Three wire formats hand-mirrored** as separate write and read functions. *The rules format is
now one list; snapshots and orders remain — see §4.*

**Determinism verified only within one process.** *Fixed: the scripted match's final hash is
pinned to a value computed under clang, so MSVC Debug and Release must agree with a second
toolchain.*

**No sanitizer configuration**, though a test comment claimed one would catch overreads. *Remains.*

**Main page: hit regions produced inside drawing**, so taps cannot be tested; ADR-040 names the
fix. *Remains.*

**"Space MMO" in AGENTS.md.** Nothing in the design is massively multiplayer. *Fixed.*

### Performance and scale

At the design scale of six to twelve players and four ticks a day there is nothing to optimise:
resolution is measured in microseconds, a whole-match replay in low milliseconds, and every phase
is a linear walk over tens of items. The scale axis that matters is matches per server, not
players per match, and that is where the shape stops: one process, one match, one port, one store
name, a polling server thread, no connection cap. The seam contains the eventual fix. The client
redraws at vsync forever for a screen that changes four times a day; throttling when idle is the
one real cost today. *All remain — see §4.*

## 3. The owner's answers, and what was done

| Question | Answer | Done on this branch |
|---|---|---|
| Wire real resumption, or keep restart = new match? | Wire it | ADR-042: UTC instant, store v2 with schedule, tokens and a finished flag, `Session::Resume`, load-or-create in both roles, finished store moved aside, corrupt store fatal |
| Is the comment history deliberate? | Sweep it tree-wide | Every history-only comment in five libraries rewritten; rule in AGENTS.md §4, check in §7 |
| Free `GameLogic` from the Windows headers? | No, keep uniformity | Nothing; the clang shim build stays a verification aid outside the tree |
| Scope | Defects and Phase 0 blockers | Everything marked *Fixed* above, in eleven commits |

## 4. What remains, in the order it will matter

1. **A shared store name.** Two servers beside one executable overwrite each other's store and
   log. A `--store <name>` flag, or a name derived from the port, is a small change.
2. **Blocking connect on the render thread.** A black-holed host freezes the window in bursts
   every two seconds while reconnecting. Connect non-blocking and poll, or connect on a helper
   thread.
3. **Client library.** A `LockstepClient.lib` holding the view model and the pages, with a thin
   executable over it, removes the double compile and the precompiled-header trap the test project
   documents. R13 is about runtime files; a static library ships nothing.
4. **One bidirectional serialize function** over a reader-or-writer archive for snapshots and
   orders, as the rules format now has in miniature. Halves the wire code and gives one place to
   range-check enums.
5. **Layout separated from drawing in `MainPage`**, so taps can be tested and the file shrinks.
6. **Sanitizer configuration** for the core and server suites, so the fuzz tests mean what their
   comments say.
7. **Idle throttling** in the client: render on input or new state, or at a low rate, when nothing
   has happened for a few seconds.
8. **IPv6**, or at least a resolver that accepts it.
9. **Release in CI** for `GameLogicTests` at least, now that the pinned hash gives it something to
   disagree about.
10. **Matches per server**, if that ceiling is ever reached: a server owning a vector of sessions
    keyed by a match id in the hello, one listener with a poll call, a store path per match.
11. **Reconnecting players** are still owed the digests they missed (ADR-028, ADR-042).
