# ADR-097 - The last screen shows the whole table

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 3.4 at the owner's instruction to work the plan. The item's [ASK] is answered by the wire: the standings are already on it.
**Supersedes:** -

---

## Context

`MATCH FINISHED` said `6 OF 6 · SCORE 35 · LEADER P6 95`: where the reader came, what they scored,
and who won. Six players, three numbers, four empires unaccounted for -- on the one screen whose
entire job is to say how the match went.

UI-01 3.4 marked this **[ASK]**, because it was not known whether per-player scores reach the
client. **They do.** `SnapshotStanding` carries a player, a score, a placement and a status for every
player and has since the snapshot existed; `ViewOf` copies it into `MatchState::players` as
`PlayerBadge`. Nothing needed to be added to the wire, so the [ASK] is answered by reading the code
rather than by a decision.

## Decision

**One row per player, in placement order: `1ST  P6  95`.** Monospace, because it is a table and the
columns have to line up. **The reader's own row is drawn in their own blue**, which is the same rule
the map and every rail use (ADR-027) and means the eye finds it without reading.

**`Facts::standings` becomes a vector of rows composed by the match loop.** `ConnectionDialog` lives
in `LockstepClient` and knows nothing about a match (ADR-038); the composition root is where
`MatchState` and the dialog meet, so it is where a table is built from one.

**`Paragraph` gains an ink, defaulting to transparent meaning "the body's".** It is the smallest
thing that lets one line in a dialog be a different colour, and only the standings use it.

**`MainPage::FormatPlacement` becomes public.** The table spells `1ST` the same way the top bar's
chip does, and two copies of an ordinal rule is one place for `1TH` to appear.

## Consequences

- **The dialog grows with the player count** -- it is already measured before it is drawn and
  centred vertically, so twelve players is a taller card and not a clipped one. At twelve rows it is
  about 400px in a 720px screen.
- **The sentence it replaced is gone**, including `LEADER P6 95`. The leader is the first row.
- A custodian's row says nothing about being one. `PlayerBadge` carries `custodian` and the table
  does not use it, which is a thing to add when somebody decides what it should say.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 05's MATCH FINISHED entry. Done in this commit.
- **Code:** `LockstepClient/ConnectionDialog.{h,cpp}` (`Facts::Standing`, `Paragraph::ink`),
  `LockstepClient/MainPage.{h,cpp}` (`FormatPlacement` made public), `Lockstep/Lockstep.cpp` (the
  table). Done, built and photographed.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `AFinishedMatchOffersTheLastDigest` and `FaceRuleTests` carry the new shape and
pass, which is what says the rows reach the draw and obey the face rule. All 154 methods pass.

Photographed on 2026-09-14 at the end of a `--phase0` match: six rows, `1ST P6 95` down to
`6TH YOU 35`, the reader's row in blue.

## Open questions

**Whether a custodian's row should say so.** The data is there. A match where somebody dropped out is
a different story from one where they were beaten, and the table currently tells both the same way.
