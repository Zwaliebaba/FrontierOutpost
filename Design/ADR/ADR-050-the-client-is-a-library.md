# ADR-050 — The client is a library

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, on the codebase review's §4 item 3, 2026-09-12.

**Corrects:** ADR-040's argument that a library "would change what ships".

---

## Context

ADR-040 gave the executable a test project by compiling its translation units a second time, and
argued that the alternative — a library — was *"a change to the shape of what ships, made for the
benefit of the tests"*. **The review says that is wrong, and it is**: a static library is a link
unit, not a runtime file, and R13 is about what sits beside `Lockstep.exe` when a player runs it. A
`.lib` sits beside nothing.

By today the double compile was nine translation units, and every new file in the client had to be
registered in two projects or the test DLL stopped linking — which is how ADR-045 was reminded of
it, mid-refactor.

## Decision

**`LockstepClient.lib` holds the client, and it does not know the game exists.**

That last clause is the whole design. `MatchState`, `DigestView`, `DesignTokens`, `MapView`,
`MapRender`, `MainPage`, `JoinPage`, `ConnectionDialog`, `MatchConnection` — the view model, the
screens, the map renderer and the socket — reference `NeuronClient` and `NeuronCore` and **nothing
of `GameLogic`**. AGENTS.md §2 has always said *"`GameLogic` is referenced by the executable and by
nothing else; the day a client-side file reaches for it is the day the server stopped being
authoritative"*, and until now that was a sentence. It is a build edge.

**Three files stayed in the executable, and they are exactly the ones that reach `GameLogic`:**

- `HostedServer` — bridges `NeuronServer` and `GameLogic`.
- `SnapshotView` — turns `GameLogic`'s wire records into `MatchState`.
- `SeatsPage` — names `BotPolicy` for its roster.

A bridge belongs in the composition root, which is where this tree already puts them. They are the
only three `LockstepTests` still compiles twice, down from nine.

**It was not a choice to put them there.** The split is not taste: it is where the dependency
actually falls, and the fact that it falls so cleanly — nine files with no game dependency, three
with one — is the evidence that the layering was right all along and merely unenforced.

## What this does not do

**`SeatsPage` could be moved with a mapping.** It needs `BotPolicy` only to build a roster and name
three styles; a client-side `SeatStyle` enum and a conversion in the composition root would put it
in the library. That is a duplicated enum and a mapping function to avoid one edge, and R2 says add
a layer when a second thing needs it. If a second screen ever names a game type, this is the shape
to reach for.

## Consequences

**Eleven projects.** `AGENTS.md` §2's tree, its build command, `CheckProjectFiles.py`,
`RunClangTidy.py` and the solution all name it. `RunClangTidy` checks 71 translation units where it
checked 70 — the library's nine minus the six that stopped being compiled twice, plus its `pch`.

**The precompiled-header trap is mostly gone.** `LockstepClient` has its own `pch.h`, which includes
`NeuronClient.h` for the same load-bearing include order every screen needs. Only the three bridge
files still carry `PrecompiledHeader NotUsing`.

**Nothing about `Lockstep.exe` changed**, which was the fear ADR-040 recorded and the thing the
review corrected. 436 tests pass in both configurations.
