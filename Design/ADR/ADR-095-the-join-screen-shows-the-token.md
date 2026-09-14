# ADR-095 - The join screen shows the token and stops promising

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 3.2 at the owner's instruction to work the plan.
**Supersedes:** -

---

## Context

The join screen is the first thing anybody sees. Four things on it were wrong, and three were
promises the tree does not keep.

**The token was masked.** It is the one string the screen exists to get right -- a player reads it
off a message and types it in -- and it was the one string they could not check. `SHOW` was there,
which means the decision was "masked unless asked".

**`LAST USED` labelled the server field.** Nothing is remembered between runs: the client writes no
files (R13). The field is prefilled from the command line or from this process's own host, which is
a different thing and not what the label says.

**`SEAT · NOT YET CONFIRMED` was a dashed box that was never populated.** `SetSeat` was called once,
on `Status::Playing` -- at which point the screen is replaced by the board in the same frame.
`Design/UI/README.md` has recorded `SetMatchSummary` as having no caller since the record was
written; the seat box was the same defect one line up.

**`ALSO: --join SERVER TOKEN`** taught a command line to somebody who had already found the screen
that replaces it.

## Decision

**The token is shown by default and the control reads `HIDE`.** A token names a SEAT and not a
person; ADR-029 is explicit that it is not authentication, and the screen's own body text says
*"whoever types it plays that empire"*. It is read aloud between friends and pasted into chats.
`HIDE` stays, for somebody sharing a screen.

**`LAST USED` is removed.** A label that describes a feature the binary cannot have (R13) is worse
than no label.

**The seat box and `SetMatchSummary` are removed**, with the members behind them, and the card is 48
pixels shorter. The seat a token bought is on the top bar a frame later, in the colour the whole map
is drawn in.

**The footer is removed and the flags are documented in `Design/GETTING-STARTED.md`**, where
somebody looking for a command line is looking.

## Consequences

- **The card is 234px rather than 282px** and the screen is quieter: two fields, a sentence, a
  button. Every capture of screen 03 and 05 is stale.
- **A player on a shared screen has to press `HIDE`.** That is the trade, and it is the right way
  round: the common case is one person at one machine reading a token they were sent.
- `JoinPage` lost four members and two methods. Nothing else called them.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 03, `README.md` (the `SetMatchSummary` finding is resolved),
  `Design/GETTING-STARTED.md` (the flags table gains `--dev` and a note about the footer).
  Done in this commit.
- **Code:** `LockstepClient/JoinPage.{h,cpp}`, `Lockstep/Lockstep.cpp` (the `SetSeat` call).
  Done, built and photographed.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `TheTokenIsShownAndHideIsTheControl` -- renamed from `ShowRevealsTheToken` -- pins
that the token starts shown, that something on the screen hides it, and that it can be shown again,
and that neither alters the field. The other `JoinPageTapTests` pass unchanged, which is the claim
that the moved geometry did not break a target: they sweep for controls rather than for coordinates.
All 154 methods pass.

Photographed on 2026-09-14: `RMPJ-CV63` legible in the field, `HIDE` beside `TOKEN`, no `LAST USED`,
no seat box, no footer.

## Open questions

**None.**
