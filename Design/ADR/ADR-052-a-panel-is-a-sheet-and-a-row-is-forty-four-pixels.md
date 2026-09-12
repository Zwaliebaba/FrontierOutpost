# ADR-052 — A panel is a sheet at the bottom, and a row is forty-four pixels

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, on a screenshot of the destination picker: *"the move fleet screen is not very mobile friendly."*
**Supersedes:** —

---

## Context

`MainPage::DrawPanel` draws all four of the game's pop-up panels — build, destination, signal,
replay — from one body of code. Until this decision it drew them as a 300-pixel card floating in the
middle of the map pane, at a fixed offset below the top bar, with rows 20 pixels high carrying one
string each and a close target 24 pixels wide.

Three things are wrong with that, and only the first is about fingers.

**A 20-pixel row is not a target.** `PointerInput` was written touch-first by decision rather than
accident (`Design/Reference/mobile-portability.md` §8): `EnableMouseInPointer` collapses mouse and
touch onto one path, so every row in this list is something somebody may put a finger on. Twenty
pixels is roughly half the smallest target a finger hits reliably, and the four destination rows in
a lane picker sit directly on top of each other, so a miss is not a miss — it is the wrong
destination, ordered, silently.

**A card in the middle of the pane covers the thing the choice is about.** The destination picker's
subject is the map: which lanes leave this system, what is at the far end, who else can reach it.
Centred, the panel sits on top of exactly the systems being chosen between, and it puts the list
under whichever hand is reaching for it.

**One string per row meant the picker could not say what it knew.** It listed `Faroe - ETA T1` and
stopped. Whose Faroe is, whether it is a capital, whether it is contested, how many ticks the lane
costs as distinct from the tick of arrival — the client has every one of those in `MatchState` and
had nowhere to put them. A player deciding where to move a fleet is deciding between an expansion
and a fight, and the screen was not telling them which was which.

**What is not in question is the frame.** 1280×720 is a design constraint, not a setting
(`Design/README.md` §1), and there is no mobile build of this tree — R12 fixes the client to D3D12
on Windows. `mobile-portability.md` §5 records that a fixed landscape framebuffer cannot be
presented in portrait at all, at any scale anyone would ship. So "mobile friendly" here cannot mean
a phone layout, and this ADR does not pretend it does. It means touch-first inside the frame the
game has: targets a finger can hit, controls where a thumb already is, and a row that carries what
the decision needs.

## Options considered

### A. Keep the card, enlarge the rows

The smallest change: 20 pixels becomes 44 and the card grows. It fixes the target size and neither
of the other two problems — the card still covers the map, and now covers more of it, because a
six-row list at 44 pixels is 264 pixels tall in the middle of the pane.

### B. A sheet anchored to the bottom of the map pane

The panel spans the pane's width less a margin and sits against the bottom edge. Rows are 44 pixels;
a row carries a title, a second line and a right-aligned status; the header keeps its `X` and a full
width `CANCEL` bar is added under the list.

The map above it stays visible — a six-row sheet is 340 pixels of a 676-pixel pane, so more than
half the map is still on screen and the systems near the top, which is where the camera frames them,
are untouched. The thumb reaches the list without crossing the map. And the row has room for the
three things a move is decided on.

The cost is that a long list no longer fits. Six rows is the cap, and a seventh has to be reported
rather than drawn.

### C. Make the panel scroll

Keeps every row reachable at any length. It needs a scroll model this client does not have — no
panel, rail or list in the tree scrolls today, the digest included, and adding one for the panel
alone means a gesture that competes with the map drag underneath it (`PointerInput` has no concept
of a gesture owned by a region). A real answer eventually, and a large one for a list that is
usually four rows.

### D. Put the destinations on the map instead

Tap the fleet, tap the system. No panel at all, and the most direct thing a map game could do. It
needs hit-testing against projected node positions through a camera the player can orbit and zoom,
and it has no way to show a lane cost, an ETA and an owner before the tap is committed. Worth
revisiting when the map has a selection model; it is a bigger change than this one and it does not
help the build, signal or replay panels, which have nothing on the map to tap.

## Decision

**B.** A panel is a sheet at the bottom of the map pane. **The row height is 44 pixels and every
other number follows from it** — it is the smallest target a finger hits reliably, and it is also
`Frame::TOP_BAR_HEIGHT`, so a row and the bar read as the same unit of the frame.

A row carries an owner square, a title, an optional second line and an optional right-aligned
status, and **the row height does not change with the content**: one line centres, two sit either
side of the middle. A column of rows of one height is what a finger aims at, and a list whose rows
change height as the board changes is one where the target under the thumb moved between sessions.

**The destination picker uses all four fields**, and that is the point of having them: the square is
the owner's colour (ADR-027), the second line says whose the system is and whether it is a capital or
contested, and the right-hand column says the lane cost and the tick of arrival as two separate
facts. The other three panels leave what they have nothing for empty and draw single-line rows.

**The close control is doubled.** The header keeps its `X`, now a 36-pixel square rather than a
24-pixel strip, and a full-width `CANCEL` bar closes the sheet from where a thumb already is.
Closing a sheet opened by mistake is the commonest thing done to one.

**Six rows, and a seventh is reported rather than dropped.** `SHEET_MAXIMUM_ROWS` is six; beyond it
the sheet draws a muted row saying how many did not fit. A picker that quietly forgets a lane cannot
be trusted about the ones it did show.

## Consequences

**Every panel got the same treatment, because they are one function.** The build and signal lists
gained 44-pixel rows and a status column — a queued build now says `QUEUED` on the right rather than
inside its own title — and the replay stub reads as a list rather than a paragraph. That is wider
than the change asked for and it is not optional: they share the code.

**Half the map stays visible with a full sheet open**, measured at the reference layout: the pane is
676 pixels tall below the bar and a six-row sheet with a header and a cancel bar is 340, leaving 324
above it plus the 12-pixel margin.

**A lane list longer than six is now possible to hit.** The generator's frontier lanes make a
six-lane system unlikely but not impossible, and before this the overflow was silent in a different
way: every row was drawn, the card grew past the bottom of the screen, and the last ones were
unreachable rather than merely unlisted.

**It does not make this game playable on a phone**, and nothing here claims to. The three decisions
`mobile-portability.md` §10 leaves open — which game, whether 1280×720 is a per-platform constant,
whether the single error path stays — are untouched. What this buys toward that day is that the one
screen a player uses most is already laid out for a finger, so a port's UI work starts from a design
that assumes one rather than from a mouse layout that has to be redrawn.

**There is still no scrolling anywhere in this client**, and the cap makes that visible rather than
fixing it. Option C is the eventual answer and this is not it.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` gains the sheet under screen 01. `DESIGN-GUIDELINES.md`
  gains the sheet component beside the dialog.
- **Code:** `LockstepClient/MainPage.h` gains the five `SHEET_*` constants;
  `LockstepClient/MainPage.cpp`'s `DrawPanel` is rewritten around a `SheetRow`. Done, built, and
  run.
- **AGENTS.md:** nothing.

## Open questions

**Whether the digest and the locks rail should follow.** They are the other two lists a finger
touches, and their rows are the design sheet's event rows rather than 44-pixel targets. Changing
them is a change to screens 01 and 06 as drawn, which is a larger decision than this one.

**Whether the sheet should animate in.** It appears and disappears instantly, which on a screen that
redraws only when something changes (ADR-047) is honest but abrupt. Nothing in this client animates
and starting here would be an odd place to begin.

**Whether `CANCEL` should say what it cancels.** On the destination picker it closes without
ordering anything, which is right; on the build panel the rows have already toggled by the time it
is pressed, so it closes rather than undoes. The label is the same in both and the behaviour is not.
