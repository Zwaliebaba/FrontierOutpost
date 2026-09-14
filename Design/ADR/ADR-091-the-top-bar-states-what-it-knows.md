# ADR-091 - The top bar states what it knows, and hides what is not finished

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 3.1 at the owner's instruction to work the plan.
**Supersedes:** -

---

## Context

Three things on the 44-pixel bar, each wrong in its own way.

**`M0007` is not a match id.** The snapshot carries none, so `SnapshotView` fills the field with the
zero-padded TICK. The result is a four-digit number that changes every tick, sitting beside `D3/21`
and `T9 LOCKS` which are both also about time, and looking like the one thing on the bar that is
stable. `Design/UI/README.md` has recorded it as a finding since the record was written.

**`▶ REPLAY T7` opens a sheet titled `REPLAY TICK 7 - NOT YET WIRED`.** Screen 07 needs
`PhaseRecord`s the snapshot does not carry. A control that says its own screen is unfinished teaches
a player that the buttons here may not do what they say -- which is precisely the lesson ADR-053
spent itself unteaching for builds and ADR-077 for fleets.

**The placement chip is the same ink at 1st and at 6th.** Public score is the one-pager's
anti-snowball instrument and it is there so a player can tell they are falling behind; a chip that
only ever states a number says where you are and never that you are sliding.

## Decision

**The census loses the stem's match id**: `D3/21 · 6 PLAYERS · 31 SYSTEMS`. The clause-dropping order
that trims the left half to fit is unchanged, and it now has one fewer clause to drop.

**`REPLAY` is drawn only under `--dev`**, a new startup flag that is off in every shipped run and
means "show the controls for screens that are not finished". The sheet behind it drops
`- NOT YET WIRED` from its title when it can only be reached that way: **the flag is the
disclosure.** One control is behind it today.

**The chip carries the direction as well as the place.** Blue when you lead, amber when your
placement is worse than the one this client last drew, the ordinary outline otherwise.
`m_placementDrawn` is recorded in `Create`, from the state being replaced -- so it is the place on
the previous DIGEST, which is what "since the last digest" means. It is session memory and nothing
more: it starts at zero, which is no placement, so a client that joins mid-match or reconnects
simply has no previous place and the chip is not amber until it has drawn one.

## Consequences

- **A player can no longer tell two matches apart from the bar.** They never could -- the field was
  the tick -- so what is lost is the appearance of being able to. When the snapshot carries a real
  match id the clause comes back.
- **Screen 07 is now unreachable in a shipped build.** It was a stub, `Design/UI/SCREENS.md` records
  it as one, and its capture `07-replay.png` is now of a `--dev` screen rather than of the game.
- **The amber chip is the first thing on this screen that depends on what the client saw last.**
  Everything else is a pure function of the current state. It is one `std::uint32_t`, it degrades to
  "no signal" rather than to a wrong one, and the alternative -- putting the previous placement on
  the wire -- is a field the server would carry for one chip.
- Amber now means three things on the bar: the countdown, a rival's colour, and a lost place. That
  is the economy ADR-027 asked for; all three are "look at this".

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01 and 07, `README.md` (the `M<id>` finding is resolved).
  Done in this commit.
- **Code:** `LockstepClient/MainPage.{h,cpp}` (`SetDeveloperControls`, `m_placementDrawn`, the
  census, the chip, the replay sheet's title), `Lockstep/Lockstep.cpp` (`--dev`). Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `TopBarTests` sweeps the bar and pins that a shipped build cannot reach the replay
sheet and that `--dev` puts it back, and asserts through the view model that the match id is still
the tick -- so that the day it stops being, the test says so and the clause can return. All 152
methods pass.

**Not photographed** -- captures are being taken as one pass at the end of UI-01.

## Open questions

**Whether `--dev` should gate anything else.** It exists for one control. The candidates are the
things `SCREENS.md` lists as stubs, and none of them is currently reachable at all.
