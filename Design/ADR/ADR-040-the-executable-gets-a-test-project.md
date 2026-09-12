# ADR-040 — The executable gets a test project

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, "implement", 2026-09-12, on the open question ADR-039 left.

---

## Context

Four libraries had a test project. `Lockstep` did not, because it is an executable — R13 says the
game ships as one file — and a test DLL cannot link one.

What that left untested was not glue. It was every decision the client makes:

- `ViewOf`, which turns a snapshot into everything on the screen;
- `OrdersOf`, which turns the screen back into an order set the server will act on;
- `ComposeSignals`, which decides what a player is allowed to say to whom;
- `DigestView`, whose ranking and grouping decide what a player reads first;
- `MainPage::FormatCountdown` and the lock, which decide when a player stops being able to change
  their mind.

**It had gone wrong twice in two days, in the same way both times.** ADR-038's connection dialogs
and ADR-039's signals were each proved by a harness compiled outside the repository against the same
libraries — a real check, and one that has to be rebuilt by hand before it can be run again, which
means it is run once. And `FormatCountdown` — six lines of arithmetic, no dependencies, trivially
testable — had truncated instead of rounding up for the whole life of the screen, so `00:00:00` sat
on the top bar for the entire last second while the rail still accepted edits. It was found by
**photographing a running client**. That is not a reasonable way to find a bug in a pure function.

## Decision

**`Tests/LockstepTests` is the tenth project**, and it compiles four of `Lockstep`'s own translation
units a second time: `MatchState.cpp`, `SnapshotView.cpp`, `DigestView.cpp`, `MainPage.cpp`.

**The alternative was to carve the client's view model into a fifth library**, which is a change to
the shape of what ships, made for the benefit of the tests. This way nothing about `Lockstep.exe`
changes at all — the same files, compiled again, into a DLL nobody installs.

Two details that are load-bearing rather than incidental:

- **`PrecompiledHeader NotUsing` on every borrowed file.** They open with `#include "pch.h"`, which
  resolves to `Lockstep/pch.h` because cl searches the including file's own directory first. Under
  `/Yu` the compiler would substitute *this* project's precompiled header for that line and compile
  the file against the wrong prologue.
- **`Tests/LockstepTests/pch.h` includes `NeuronClient.h`**, which none of the other test projects'
  do. The view model reaches the renderer's types, and `NeuronClient.h` is where the load-bearing
  include order lives — `NeuronCore.h` first for `<windows.h>`, then `d3d12.h` and `dxgi`, which
  both assume it. Without it a test that includes `MainPage.h` fails inside `DescriptorHeap.h` on
  `ID3D12Device`, several headers from anything it wrote.

**What belongs in it: everything in those files that DECIDES something.** The drawing does not — a
`DrawWorld` needs a device, a swap chain and a frame, and what it produces is pixels nobody can
assert about. Screens stay verified by photographing them (ADR-038, ADR-039). The line is exactly
that: if it decides, it is tested here; if it draws, it is a screenshot.

**`SignalTests` drives a real match; `DigestViewTests` builds its state by hand.** That difference
is deliberate. `ComposeSignals` asks questions *about a board* — does this lane have one of my
systems at one end and a rival's at the other, has this empire actually been met — and a fixture
built to answer them would be a fixture built from the same assumptions the code makes. A generated
galaxy with fourteen ticks of bots played on it is a board neither of them arranged. A digest, by
contrast, is whatever the server chose to say, and the grouping rule needs combinations a real match
produces rarely.

## What the first run found

Thirty-five tests; thirty-two passed, and the two failures were both worth having.

**`DigestCard::actor` contradicted its own header.** The header says *"Set when this card groups a
player's events. `NOBODY` on a plain event card"*, and the ungrouped path copied `event.actor` onto
the card anyway — so `card.actor != NOBODY` was not a test for an actor card, which is the obvious
way to ask and the way the header invites. Nothing drew from it yet, which is precisely why it was
worth fixing now: the obvious first use is an owner swatch, and it would have gone on cards that are
not about that owner. Fixed to match the documented contract.

**The delta test was wrong about the rule, not the code.** `DeltaOf` returns nothing when
`unreadTicks` is zero, because the four-cell box belongs to the `SINCE YOU LOOKED` header and a
player who watched every tick has not missed anything. The test now pins that as a rule of its own
rather than tripping over it.

## Consequences

**Ten projects.** `AGENTS.md` §2's tree, its build and test commands, `Build/CheckProjectFiles.py`,
`Build/RunClangTidy.py` and the CI workflow all name the fifth suite now. CI runs 388 tests where it
ran 353.

**`RunClangTidy.py` skips a borrowed translation unit.** A `.cpp` reached by a relative path is
checked where it lives; linting it twice would double every finding in it and report the same line
under two project names.

**The throwaway harnesses are superseded.** The checks ADR-039's scratch harness made — a border
offers a lane, a sent offer comes back as something to withdraw, conceding reaches the server and
ends the empire — are now tests, and they run on every push.

## Open questions

**`MainPage`'s tap handling is compiled in and barely tested.** `HandleTap` is reachable without a
GPU, but the hit list it scans is built during `DrawInterface`, which is not. Testing a tap
therefore means either drawing first or splitting the hit list out of the draw — the second is the
right shape and is a change to `MainPage`, not to this project. It would let the buttons ADR-038 and
ADR-039 left unpressed be pressed by something other than a finger.
