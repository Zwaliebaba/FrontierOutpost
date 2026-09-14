# ADR-111 — A sheet is about a place, and it is the only door an order goes through

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner, in the place-sheet interaction handoff of 2026-09-14
(`design_handoff_place_sheet/`, frame `1a`) and the prompt that carries it.
**Supersedes:** — (amends ADR-058's subject, ADR-079's sheet, and ADR-052's body)

---

## Context

There are two orders a player gives on this screen and they enter through different doors.

**A build enters at a place.** Tap a system you hold and its build sheet opens (ADR-058); the sheet
is a 2×2 grid of tiles, one per thing that system can build (ADR-107). A digest button priced
`SHIPYARD JANDAL 20 CR` queues the same order from the other column without opening anything
(ADR-053), and a queued row on the locks rail is a link back to the sheet (ADR-060).

**A move enters at a fleet.** Tap a `FLEETS` row on the locks rail — or a garrison badge on the map
(ADR-079) — and a destination picker opens: a column of 44-pixel rows, one per lane out of where the
fleet stands, sorted by arrival (ADR-092). A system holding two of your fleets opens a third sheet
first, which exists only to pick between them.

So one screen has four sheets, three entry points and two body shapes for one question — *what can
I do here this tick?* — and the answer is split by the KIND of the order rather than by the place it
is about. A player looking at Dothan sees its builds; the ten ships standing on it are in a column
on the other side of the screen, under a heading about fleets.

Two further things make the split cost more than it looks.

**The digest gives orders without opening anything.** A priced build button queues against a purse
the column does not show, minus a queue the column does not show; ADR-078 added a sentence to the
*sheet* to say what the tiles are priced against, and the button that places the same order from
400 pixels away has no such sentence and no room for one.

**And the map is idle while a move is chosen.** The picker is a list of lanes drawn over a map that
is already showing those lanes, their tick costs and what is standing at the far end.

## Options considered

### A. Keep the four sheets and add cross-links

A `FLEETS HERE` line on the build sheet linking to the picker, and a `BUILD` line on the picker
linking back. It is the smallest change and it keeps every sheet's body as it is. It also keeps the
four doors, adds two more, and answers none of the digest's problem.

### B. One sheet per PLACE, holding the builds and the fleets

Tapping a system you hold opens a sheet whose body is the build grid *and* a `FLEETS HERE` section
with one row per fleet of yours standing there, each carrying a `MOVE ›`. Every door — the map's
disc, its garrison badge, a digest button, a rail row — opens that one sheet, and no digest control
places an order any more.

It costs the sheet's height: a full grid, a divider, a band and two fleets is 419 pixels of body
before the header and the bar, against ADR-052's rule that more than half the map pane stays map.
So the body has to be capped and scroll, which is a fourth scrolling region on this screen.

### C. One sheet per place, with the fleets behind a tab

Same sheet, two tabs: `BUILD` and `FLEETS`. It keeps the sheet short and it puts the two things a
place can do behind a control that has to be discovered, on a screen whose whole argument is that a
player opens it twice a day and should not have to hunt. A tab is also the one component in
`Design/UI/README.md`'s not-built list that nothing has yet needed.

### D. One sheet per place, and let it be as tall as it needs

Drop ADR-052's half-pane rule for this sheet. It is the simplest implementation and it takes the
map away at exactly the moment the choice is about the map — a full grid and two fleets would put
the sheet's top at y=245 of a pane that starts at 44.

## Decision

**B.** There is one sheet about a place, it holds everything that place can do this tick, and it is
the only surface an order is given on.

**The sheet.** Header 44: a 10-pixel disc in the owner's colour, the system's name in the display
cut, then a muted clause saying what the place IS — `YOURS · +6 A TICK · CAPITAL` — then the status
slot (the `LOCKED`/`OFFLINE` chip, else the `RISING · DONE T14` chip, else the purse) and the 44×44
`X`. Then the help line, unchanged in its three sentences and their order (ADR-065, ADR-070,
ADR-078). Then the body: a `BUILD` band with `2 AVAIL · 1 AT A TIME`, the tile grid, a divider, a
`FLEETS HERE` band with the ships standing there, and one boxed 44-pixel row per fleet. Then a
44-pixel bar reading **`DONE`** rather than `CANCEL`, because there is nothing to back out of: what
was ordered on it is already in, and closing it is finishing.

Seven things inside that had alternatives.

**1. The body is capped at half the pane and scrolls, and it scrolls by BLOCKS.** The cap is what is
left of ADR-052's 338 once the header, the help sentence and the bar have taken theirs, floored at a
band and one row of tiles. The locks rail scrolls in pixels because every row on it is 44 and every
offset lands somewhere legible (ADR-101); this body's tallest block is a 96-pixel tile row in a
viewport that can be 124, and `ShapeRenderer` has no clip rectangle — so a part-scrolled tile is
either painted over the help line above it or dropped whole. The digest made the same trade for the
same reason and scrolls by whole cards (ADR-080). A wheel notch over the sheet moves one block, and
**a drag that began on the sheet moves it too**, banked to 44 pixels: a wheel is not a finger and
this game is for touch (ADR-098). That drag is why this body has no page band where the locks rail
needed one (ADR-101) — the rail cannot be dragged at all.

**2. The `FLEETS HERE` section is pinned above the bar when the body scrolls — all of it or none of
it.** Pinning follows ADR-093's shape and changes its subject: the concede is pinned because it must
always be reachable, and the move is pinned because it is what a player came to this sheet for while
the grid is what overflows. **The band alone is not enough**, which is the reading the handoff's
sentence invites: a label above the bar with `MOVE` still behind the scroll is the opposite of what
pinning is for. And a couple of rows with the rest hidden would be a sheet that quietly forgets a
fleet, which every other sheet on this screen refuses to do. So the whole section is pinned when it
fits in half the body, and otherwise it scrolls with everything else.

**3. A fleet belongs to the place it was ordered OFF, not the place it is standing at.** A move given
this tick puts a fleet on a lane at progress zero from the moment it is given (ADR-055) while
leaving it where it is until the lock (ADR-077), so a list of fleets *standing* at a system loses the
one the player just ordered — which is exactly the row they need in order to take it back.
`FleetsAtPlace` keys on `from`, and the row reads `10 SHIPS → FAROE · T1` with a **committed**
`TAKE BACK` where an unordered one reads `10 SHIPS · HOLDING` with an outlined `MOVE ›`.

**4. No digest control places an order; each of them opens the place it is about.** A priced build
button becomes `BUILD AT DOTHAN | 20 CR` and opens Dothan's sheet, where the purse and the queue are
on the header and the sentence under it says what the tiles are priced against (ADR-078). A `MOVE
FLT 1 | 10 SHIPS` opens the sheet for the system that fleet is standing on. **The button's action
and its index are chosen together** (ADR-057), because the index the screen needs — a system
position — is not the index the digest named. Nothing about ADR-053 or ADR-056 changes except where
the tap lands: the price is still on the button, and the standing moves are still what a digest with
nothing to act on offers.

**"Scrolled to the relevant control" is satisfied by the layout rather than by a scroll position.**
The handoff asks a digest button and a rail row to open the sheet scrolled to the tile or the fleet
they name. The `BUILD` band is the body's first block and the `FLEETS HERE` section is pinned
whenever the body scrolls, so both are on the screen at scroll zero in every case the sheet can
produce — and a scroll target that is always zero is a field nobody can keep true.

**5. The garrison badge and the disc under it now behave alike, which answers ADR-079's open
question.** The badge went focus-only at the lock while the disc beside it opened a sheet; they were
two rules because they opened two different things. They open one sheet now, so at the lock the
badge opens it and it is inert, like every other sheet (ADR-065). They remain two targets, because
they still name two things: the system, and the ships standing on it. The sheet that existed only to
pick between several fleets is gone — the place sheet lists them all.

**6. An inert tile's border is dashed and its ink goes back to `NEUTRAL_DIM`, so
`Ink::TILE_BLOCKED_INK` is retired.** ADR-107 put a blocked tile's ink deliberately below the
contrast floor at 3.21:1, because faint was the only channel it had for saying the tile could not be
ordered. The control vocabulary gives it a dashed border, which says that in the chrome (ADR-110) —
so the ink no longer has to, and the one exemption in this palette goes with it.
`ContrastTests::TheBlockedTilesInkIsBelowTheFloorOnPurpose` is deleted rather than inverted: the
claim it made is not true of anything any more.

**7. A sheet swallows every tap it is over, and `Action::None` is how it says so.** A sheet has
always drawn as a modal and never behaved as one for taps: only its rows were targets, so a tap on
the band between two of them fell through to the map underneath and opened a different sheet. The
place sheet has far more of that space — two bands, the gaps in the grid, the pinned block's
padding — and the case that found it is exact: a fleet marker drawn at progress zero sits under the
very sheet the move was ordered from (ADR-055). The sheet records its own rectangle first, so every
control on it still wins the space it covers.

## Consequences

**Measured on 2026-09-14, from the hit rectangles the real screens recorded** (the two that close a
sheet give its top and its bottom): the opening board's Dothan sheet is **299** pixels — header 44,
no help line, body 211, bar 44 — with two tiles and two fleet rows. The same sheet at the lock is
**338**, which is the cap exactly: the lock sentence takes three wrapped lines, so the body is
capped at 183 and scrolls. A twelve-tick board's sheet with two tiles and no fleets is **222**. A
forced four-tile grid with a fleet and a queued build is capped at 338 and scrolls, which
`PlaceSheetTapTests` asserts rather than describes.

**Two sheets became one and a third disappeared.** `Panel::BuildList` and `Panel::FleetList` are
`Panel::Place`; `Panel::Destination` is unchanged and is what `MOVE ›` still opens, until ADR-113
replaces it with the map.

**`TapTests` grew a fixture problem and it is worth naming.** Its sweeps step 8 pixels and stop at
the first effect, so they are a function of the layout: `ABuildThePurseCoversCanBeQueuedFromTheScreen`
needs two taps now where it needed one, and it uses `SweepFor`'s `_ensure` hook to keep the sheet in
front of the sweep. `ARivalsGarrisonIsReadAndNotOrdered` and the badge tests were rewritten around
the place sheet rather than the picker.

**The digest can no longer be used one-handed to queue a build**, which is a real loss and is the
price of the purse arithmetic being where the price is. A player who knows what they want now taps
twice: once to open the place, once to give the order.

**The place sheet is the first body on this screen with a scroll a player can miss.** The pinned
section means the move is always reachable; a tile in the second row is not, without a notch or a
drag. There is no band saying so, because the body is not a list of equal rows and a count of
"blocks" is not a thing a player has a name for.

## What this changes elsewhere

- **Design/:** `UI/DESIGN-GUIDELINES.md` §Frame (the sheet's share and the place sheet's blocks) and
  §Components (the place sheet, and the tile variant's inert state); `UI/SCREENS.md` 01 (the sheet,
  the map's taps and the digest's links); `UI/README.md`'s capture list. Done in this commit.
- **Code:** `LockstepClient/DesignTokens.h` (`TILE_BLOCKED_INK` retired);
  `LockstepClient/MainPage.{h,cpp}` (`Panel::Place`, `Action::OpenFleetsAt`'s meaning,
  `Action::CancelFleetOrder`, `Action::None` as a swallow, `OpenPlace`, `ScrollSheet`,
  `FleetsAtPlace`, `SHEET_MAP_SHARE`, `SHEET_BODY_MINIMUM`, the block list in `DrawPanel`,
  `TargetOf`); `LockstepClient/DigestView.cpp` (`BUILD AT <PLACE>`).
- **Tests:** `LockstepTests/TapTests.cpp` (`PlaceSheetTapTests`, the rewritten badge tests,
  `NoDigestControlPlacesAnOrderDirectly`, the shared sheet helpers), `TouchTargetTests.cpp`,
  `ContrastTests.cpp`, `FaceRuleTests.cpp`.
- **AGENTS.md:** nothing.

## Open questions

**Whether the `FLEETS HERE` band should say what it is hiding when the section cannot be pinned.**
Three fleets on one system push the pinned block past half the body and the whole section scrolls;
the band still reads `14 SHIPS`, which is true and does not say *scroll for them*. Nobody has had
three fleets on one system on a real board yet.

**Whether a rival's system should open a read-only place sheet.** It focuses today and opens nothing
(ADR-058), which was right when the sheet was a list of orders. A sheet that said `P3 · 11 +DEF ·
CAPITAL` and nothing else would be a fact a player currently has to read off a destination row.

**Whether the body wants a page band after all.** The drag is what makes the scroll reachable for a
finger, and ADR-101 decided for the locks rail that a gesture is not enough. The difference is that
the rail cannot be dragged and this can; if a capture ever shows a player stuck on the first tile
row, the answer is the band.
