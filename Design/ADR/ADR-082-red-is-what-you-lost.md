# ADR-082 - Red is what you lost, and a capture stops being news

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` items 1.4 and 2.6 at the owner's instruction to work the plan.
**Supersedes:** -

---

## Context

Two things the map does to its own bottom half.

**`CAPTURED Tn` was drawn in `Ink::RED` under every captured system, and it never went away.**
`Design/UI/screens/01-main-page.png` is a board the viewer is winning -- ten fleets, 176 credits,
third of six -- and it carries six red `CAPTURED` labels, several of them under systems the viewer
themselves took. Red on this screen means loss: it is the battle card's ink, the concede row's ink,
and the `YOU LOSE` verdict's ink (ADR-027's economy is deliberate and small, so each colour has to
keep meaning one thing). Six of them on a winning board reads as a rout. And the label stood for the
rest of the match: a capture at T1 was still being announced at T11, next to one from T8, with
nothing to say which was news.

**The legend is drawn in the bottom twenty pixels of the map pane, and a sheet's `CANCEL` bar is in
the bottom fifty-two.** Every sheet capture this project has taken -- `01-build-sheet.png`,
`01-destination-sheet.png`, `01-signal-sheet.png`, `06-at-lock-sheet.png` and the rest -- shows
`YOU  PROPOSED LANE  TRADE LANE` sliced through by the sheet on top of it.

## Options considered

### For the capture label

**A. Leave it.** It is true: the system was captured on that tick. What it is not is news, and a map
where six permanent labels compete with the two that changed this tick is a map that has stopped
ranking anything.

**B. Age it out, and colour it by who gained.** A capture is reported for a few ticks and then the
map is just the map. Red is kept for what the viewer lost.

### For the legend

**A. Skip it while a sheet is open.** One branch. The legend is a key to colours that are still on
the screen above the sheet, so losing it for as long as a sheet is open costs a player who has
already opened a sheet to do something specific.

**B. Extend the sheet to the pane's bottom edge.** Removes the overlap by removing the gap. It also
removes the 12px margin that makes a sheet read as a sheet rather than as a fourth pane, and ADR-052
chose that margin deliberately.

## Decision

**B for the label, A for the legend**, which is what UI-01 asked for in both cases.

**`CaptureIsNews` is three ticks and then nothing.** It is a named pure function in `MapRender.h`
rather than an expression inside the draw, because it is the only part of this that is a rule rather
than a drawing, and a rule that cannot be asserted without a screen does not get asserted.

**A capture the viewer gained is drawn in the owner's colour -- their own blue -- and not in red.**
That is the half of UI-01's rule this snapshot can answer: `SnapshotSystem` carries `capturedAt` and
no previous owner, so "did a rival take this FROM me or from another rival" is not a question the
client can ask. **Both of those still draw red**, which is right for one of them and wrong for the
other, and the open question below is how to tell them apart. This is deliberately a partial fix:
the common case on a winning board is your own captures reading as losses, and that is gone.

**The map is told whether its bottom edge is covered, not what a panel is.** `MapFrame::sheetOpen`
is a bool the page sets from `m_panel != Panel::None`; the map has never known what a sheet is and
does not learn here (ADR-045).

## Consequences

- **A player who looks away for four ticks is not told on the map what changed while they were
  gone.** The digest is: ADR-044 sends the whole backlog on arrival and the cards report every
  capture in it. The map is the current board, and this makes it one.
- **A rival-to-rival capture still reads as your loss** for as long as the label lasts. It is three
  ticks rather than the rest of the match, which is the mitigation, and it is not a fix.
- **The legend is gone while any sheet is open**, including the replay stub and the signal picker,
  which are not about the map at all. That is the cost of one branch over a rule per panel.
- The sheet captures will show a clean pane bottom once they are retaken, and the map captures will
  show fewer labels -- which is the point and also means the old ones cannot be compared like for
  like.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Map (the capture label and the legend), `SCREENS.md` 01.
  Done in this commit.
- **Code:** `LockstepClient/MapRender.{h,cpp}` (`CaptureIsNews`, `MapFrame::sheetOpen`, the label's
  ink, the legend's guard), `LockstepClient/MainPage.cpp` (sets `sheetOpen`). Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `CaptureLabelTests` pins the three-tick window at both edges and that a system
nobody captured claims nothing. The ink and the legend are drawing and are captures, which this
session could not take -- the desktop was locked.

## Open questions

**How the client learns who a system was taken FROM. [ASK — owner decision]** UI-01 1.4 raised it
and this ADR does not settle it. Two answers, both workable:

- **Put it on the wire.** `SnapshotSystem` gains a `capturedFrom` PlayerId. It is the honest answer,
  it costs a wire change and a snapshot version, and it reveals nothing a player cannot already
  work out from the digest entry that reported the capture.
- **Remember it in the session.** The client keeps the previous snapshot's owners and calls a system
  lost when it was the viewer's in the last state it drew and is not now. It costs nothing on the
  wire and it is wrong for a player who joins mid-match or reconnects, because their first snapshot
  has no predecessor -- exactly the player most likely to be looking at a board full of captures.

The first is what this session would choose; it is an owner decision because it changes the wire.

**Whether the legend should come back for a sheet that is not about the map.** The signal picker and
the replay stub cover the legend for no reason. A rule per panel is more code than a bool and it is
not obviously worth it.
