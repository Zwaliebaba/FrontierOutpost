# ADR-107 — A build sheet is a grid of tiles

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner, in the build-sheet v2 design reference of 2026-09-14 (`Build Sheet v2.dc.html`, options `1b` and `2a`) and the prompt that carries it.
**Supersedes:** — (amends ADR-070's sheet clause)

---

## Context

`MainPage::DrawPanel` draws all five of this client's sheets from one body of code, as a column of
44-pixel rows against the bottom of the map pane (ADR-052). A row carries an owner square, a title,
one second line and one right-aligned status, and the build sheet used all four: `Shipyard L1 -
Dothan`, `+2 ships a tick - 1 tick`, `20 CR`.

That form was right when a build was a checklist. Three decisions since have made it the wrong
shape for this sheet and for no other.

**A build row stopped being one fact and became four.** ADR-069 gave every building three levels,
each costing more, paying more and taking ticks; ADR-070 put the pair of numbers a player weighs one
level against another by onto the row's second line. So a row now has to say which building, which
level it has, which level it buys, what that pays, how long it takes, what it costs, and what the
purse has left — seven things in three strings, of which the title spends its first fourteen
characters repeating the system named in the sheet's own header.

**A build is a choice between things of the same KIND, and a move is not.** The destination picker's
rows are read in order — nearest first, then by name (ADR-092) — because a player is looking for one
lane. The build sheet's rows are read against each other: mining or shipyard, this level or the next
one, now or after the queue. A column is the shape of the first question and a grid is the shape of
the second.

**The list is fixed at four and is nearly half unbuilt.** `Design/blueprint.md` §3 fixes the shape at
four rows per system: mining station, shipyard, the bastion designed for after Phase 0, and the
*Propose* lane row `BuildRow::isTradeLane` was added for and nothing sets (ADR-039's open question,
and item 8 of `Design/UI/README.md`'s not-built list). A cap of four that is known in advance is a
grid; a cap of six that a lane list can overrun is a column with an overflow line.

**And a system that was building had a sheet with one row on it.** ADR-070 decided that the sheet
"replaces that system's offers with one row that is not a target", which is correct about what can be
ORDERED and leaves a player mid-build with nothing to plan against: the prices, the yields and the
tick counts of everything that system will be able to take next all disappear until the build lands,
which is between one and four ticks of a screen that is opened twice a day.

Two measurements bound what follows, both taken on 2026-09-14 from the baked font via
`FontRenderer::MeasurePixels`, which is `constexpr` and is the same arithmetic the renderer lays out
with. The map pane is 620 pixels wide, so a sheet is 596 and a two-column grid inset like everything
else on it gives a tile 284 wide and 260 of text. `Mining station L2 rising`, the longest title the two built buildings can produce,
is 168 pixels in the mono Medium cut. The design reference's rising detail line — *Ordered T11 · 30
credits spent · +7 a tick when it lands* — is **297** pixels in the sans cut, and 337 with the
yield's unit spelled; neither fits 260.

## Options considered

### A. Keep the rows and put the new information on them

An icon at the left of each row, a level ladder at the right, and the price chip where the status
is. It is the smallest change, it keeps one code path for all five sheets, and the row height does
not move — so no capture of any other sheet goes stale.

It does not fit. A row is 44 pixels and a line box is 17 (`FontRenderer::LineHeightPixels`), so a
row holds two lines with 10 pixels of air; a 22-pixel icon and a three-line anatomy need 96. Cutting
the anatomy back to two lines means dropping either what the level pays or what the purse has left,
which are the two numbers ADR-069 and ADR-078 respectively put there. And it leaves the second
problem untouched: four rows of one height in a 596-pixel column is 596 pixels of width spent on
three strings each.

### B. A 2×2 grid of 96-pixel tiles

One tile per thing the system can build, laid into slots by role, each carrying an icon, a title, a
level ladder, the yield and ticks, and a bottom line with the price at one end and what it leaves at
the other. The whole tile is the target, which at 284×96 is fourteen times the area of the floor's
own 44×44 (ADR-100).

It costs a second body shape inside `DrawPanel` — the build sheet becomes a grid where the other
four stay columns — and it makes the build sheet's captures stale. It does not make the others'
stale: the sheet component's frame, position, header, help slot and `CANCEL` bar are unchanged.

### C. One full-width card per building

Four cards stacked, each the width of the sheet, each with room for everything. It is the least
constrained and the tallest: four 96-pixel cards is 384 pixels before the header and the bar, which
is 472 of a 676-pixel pane and breaks ADR-052's rule that more than half the map stays visible.

### D. A grid of three columns

186-pixel tiles — `(596 − 20 − 16) / 3` — so four fit on one row and the sheet is 104 pixels shorter
still. The top line is what rules it out: an icon, ten pixels, the title, and a 24-pixel ladder
against the right edge leaves 98 for the title, and `Mining station L2 rising` measures 168. It
overruns by 70 — and `ShapeRenderer` has no clip rectangle, so an overrun is drawn over the tile
beside it rather than cut off. At two columns the same sum leaves 196, which is 28 to spare.

## Decision

**B.** The build sheet's body is a 2×2 grid of 96-pixel tiles; every other sheet stays a column of
44-pixel rows, and the sheet component around both is unchanged.

A tile is 284×96 — `(596 − 20 − 8) / 2`, the grid inset by the same `CARD_PADDING` the header and
the help line are — with 12 pixels of side padding and 10 of top and bottom,
and three lines spread down it: a 22-pixel icon row carrying the icon, the title in mono Medium and
the level ladder; the yield and the ticks in sans; and a bottom line with the state at one end and a
note at the other. **The whole tile is the hit**, so nothing on this sheet needs a grown target.

Seven things inside that had alternatives, and each is the reason this is an ADR rather than a
layout change.

**1. An empty slot is not drawn.** The four roles are mining station, shipyard, bastion, trade lane,
in that order, and a tile is drawn for each `BuildRow` the system produces — laid into that order,
packed, with no placeholder for the two that nothing composes yet. A "coming soon" tile teaches a
player that this screen's controls may not work, which is the argument ADR-091 made about `REPLAY`.
So a system with two buildings is one row of two tiles and its sheet is 104 pixels shorter than a
full one, and the day the bastion exists it appears without anything moving.

**2. A system that is building keeps its whole ladder, and `available` is what says so.** This
amends ADR-070: the sheet draws the rising tile *and* the tiles for what that system could build
next, inert, priced, and each marked with the tick it becomes orderable on. `BuildRow::available` —
a field written since ADR-069 and read by nothing — now means *the lock would start this*, and the
three places that have to agree about it read that one field: the sheet draws such a tile inert, the
digest puts no button on it (`Offerable`), and `Orders::availableBuilds` counts exactly the rows
where it is true. `ToggleBuild`'s guard is the same field. Nothing about the rules moved: the lock
refuses a second construction exactly as it did (ADR-069), and `N AVAIL` still counts what a tap
could start.

**3. The level ladder is three squares, and three is stated in the client.** Filled for a level
already held, outlined in the tile's accent for the one it buys or is rising, a hairline for the
rest. It says `L2 → L3` as a picture, which is what lets the title name the step once instead of
twice. **`MainPage::BUILDING_LEVELS` is the one number about the game's rules this screen states
rather than reads**: the snapshot carries the level a row buys and what that level costs, pays and
takes, and nothing on the wire says how many levels there are in total. It agrees with
`GameLogic/MatchRules.h`'s `BUILDING_LEVELS` by hand, and the failure mode if that moves is a ladder
with the wrong number of squares — which is why it was acceptable to state, and why it is declared
with that sentence beside it.

**4. A blocked tile's ink is below the contrast floor, deliberately.** `Ink::TILE_BLOCKED_INK` is
white at 90/255, which measures 3.21:1 over `APP_BACKGROUND` against the 4.5:1 every other token on
this screen clears (ADR-083). The tile is not READ: the help line above it says the system cannot
take another order until the build lands, and the tile is kept so a player can see what will be
orderable and what it will cost, not so they can weigh it now. At an ink that cleared the floor,
three inert tiles compete with the one thing actually happening on that system.
`ContrastTests::TheBlockedTilesInkIsBelowTheFloorOnPurpose` asserts that it *is* below the floor, so
raising it is a decision rather than a tidy-up, and the token's declaration carries the reason.

**5. The rising detail line loses a clause, because it does not fit.** The design reference asks for
*Ordered T11 · 30 credits spent · +7 a tick when it lands*, which is 297 pixels against a 260-pixel
tile. Three ways out were available: drop clauses at draw time the way the top bar's census does
(SCREENS.md 01), wrap to two lines and let the tile grow, or shorten the sentence. The sentence is
shortened to *Ordered T11 · 30 credits spent · +7 a tick* — 222 pixels, and 229 at the largest
numbers the rules produce — because the two dropped words are the two the tile says elsewhere: the
bottom line reads `2 OF 3 TICKS` and `DONE T14`, so *when it lands* is on the tile twice. Dropping
clauses at draw time was rejected for this line specifically: the clause that would go first is
`Ordered T11`, which is the only one of the three the tile does not otherwise carry.

**6. The header's status slot gains two occupants and keeps its order.** `LOCKED` / `OFFLINE` still
wins it (ADR-065, ADR-085). Below that, a system that is building wears an outlined blue 22-pixel
chip reading `RISING · DONE T14` — outlined rather than filled, because the grey chip means nothing
here can be ordered at all and this means one thing already was. Otherwise a build sheet carries the
purse, `66 CR` with ` −40` in blue when the queue has taken something, which is the same pair the top
bar carries (ADR-087) put where the tile priced `NEED 19 MORE` can be read against it. The help
slot gains a third sentence in the same way and in the same order: the lock's, then the rising
system's, then the purse's (ADR-078).

**7. `ShapeRenderer` gains a closed stroked polygon.** The four icons are built from primitives and
never from a bitmap (R13): the fleet dart the map already draws a fleet as, the diamond-section
column the map draws a mining station as, a hexagon for the bastion, and a dashed lane between a
filled square and an outlined one. Two of them are stroked polygons and the recorder had none — only
rectangles, ellipses and single lines. `StrokePolygon` is a closed polyline, and it is closed *by
the primitive* because the edge a hand-written run of `Line` calls forgets is the last one, and the
symptom is a glyph with a gap in it that reads as a rendering fault.

## Consequences

**The sheet is shorter than the one it replaces, in every case the game can currently produce.**
Header 44 + help + grid + `CANCEL` 44, where the grid is `4 + 96 + 12` for one row of tiles and
`4 + 96 + 8 + 96 + 12` for two, and the help slot is 0, 33 or **50** — a wrapped sentence takes two
lines more often than one. A system with two buildings is **200 pixels** bare, **233** under a
one-line sentence and **250** under a two-line one; a full four-tile sheet will be **304** bare and
**354** under two lines. The built sheet ADR-052 measured was 340 and ADR-078 measured at 386 with a
two-line purse sentence. More than half the pane stays map in every case the game produces today;
the four-tile sheet at 354 would be the first to pass ADR-052's 338, and the bastion and the lane do
not exist yet.

> **Corrected 2026-09-15, on the owner's instruction, and this is an exception to `Design/README.md`
> §4.** As first written this paragraph said the help slot was "0 or 33" and a full grid 337, which
> was arithmetic done from the constants before the sheet had been photographed. The captures taken
> for it the same day showed the purse sentence wrapping to two lines — `WrapToWidth` measures in
> the mono face and the line is drawn in sans, which is a defect recorded in
> `Design/UI/DESIGN-GUIDELINES.md` §Font — so the real figures are the ones above. An Accepted ADR
> is immutable except for its status line and a wrong measurement would normally be corrected by a
> new one; the owner asked for the number itself to be right, and the precedent for recording such
> an edit in place rather than pretending it did not happen is ADR-035's.

**A build tile is the largest target on the screen.** 284×96 against a 44×44 floor (ADR-100), so
there is nothing to grow and nothing a thumb can land between two of.

**Two captures are stale and the other sheets' are not.** `01-build-sheet.png` and
`01-build-rising.png` are of a form that no longer exists. The destination, signal and replay sheets
are unchanged in every dimension — the help slot's padding went from 12 pixels to 16, which moves
them by 4, and that is recorded here rather than left for somebody to find.

**`DrawPanel` has two body shapes where it had one**, and that is a real cost: the function composes
either `rows` or `tiles` and draws whichever is filled. The frame around them — the sheet's
rectangle, its header, its help slot, the clipped-count line and its `CANCEL` bar — stays single, so
what forked is the body and not the component.

**The client now names the game's buildings in one more place.** `BuildRow::building` carries
`Shipyard` / `Mining station` alone, beside the `title` that names the system, because a tile's title
must not repeat the sheet's own header. It is composed in `SnapshotView` with the rest of the row's
words, so the client still does not name a building; what it does now is state how many levels there
are (decision 3).

**A trade-lane tile is styled and unreachable.** `isTradeLane` is set by nothing and now
`BuildRow::partner` is beside it, also set by nothing. `ALaneTileIsDrawnAndPressableWhenTheFlagIsSet`
forces both and asserts the tile draws and takes a tap, so the day ADR-039's open question is
answered the first thing anybody finds out is not whether it renders.

**A tile at its top level is styled and unreachable for the same reason.** `SnapshotView` composes no
row for a building that is already at level three, so `L3 · MAX` is drawn only if a row ever arrives
with a level above the ladder. A system with everything at its top level still says `NOTHING LEFT TO
BUILD HERE` in a row, which is the one case where this sheet is a column.

## What this changes elsewhere

- **Design/:** `UI/DESIGN-GUIDELINES.md` §Frame (the sheet's real numbers) and §Components → Sheet
  (the tile variant) and §Palette (the two new tokens); `UI/SCREENS.md` 01 (the Build entry, and the
  rail's `N AVAIL`); `UI/README.md` (the two retakes, and item 8's lane row); `blueprint.md` §2 (the
  build-sheet sentence gains "as tiles"). Done in this commit.
- **Code:** `NeuronClient/ShapeRenderer.{h,cpp}` (`ShapePoint`, `StrokePolygon`);
  `LockstepClient/DesignTokens.h` (`TILE_COMMITTED_FILL`, `TILE_BLOCKED_INK`);
  `LockstepClient/MatchState.h` (`BuildRow::building`, `::partner`, and what `::available` means);
  `LockstepClient/MainPage.{h,cpp}` (the tile constants, `BUILDING_LEVELS`, `RisingSentence`, the
  grid in `DrawPanel`, `ToggleBuild`'s guard); `Lockstep/SnapshotView.cpp` (the building name, the
  rising row's ticks, cost and sentence, the rows a building system composes, `Offerable`,
  `availableBuilds`). Done, built and run.
- **Tests:** `LockstepTests/TapTests.cpp` (`BuildTileTapTests`, and `RisingSentence`);
  `TouchTargetTests.cpp`, `ContrastTests.cpp`, `FaceRuleTests.cpp`;
  `NeuronClientTests` (`StrokePolygon`). Two existing tests had the predicate `!rising` replaced by
  `available`, which is the same claim said in the field that now carries it.
- **AGENTS.md:** nothing.

## Open questions

**Whether the bastion and the research track land here or on the rail.** The grid has a slot for the
bastion and the blueprint puts research on the rail as an empire order with no system — so a sheet
about one system has nowhere to put it, and a fifth role would need a third column the measurement
in option D rules out. Both wait for Phase 0's evidence on the combat numbers (ADR-069), and this
decision reserves the slot without answering the question.

**Whether the lane tile's partner should come from the wire.** `BuildRow::partner` is a label the
client is handed rather than one it composes, which is right; nothing hands it one, which will be
part of answering ADR-039's open question rather than a separate decision.

**Whether the rising tile should count the ticks that are IN or the ticks that are LEFT.** It says
`2 OF 3 TICKS` beside `DONE T14`, which is the fleet rail's form — a progress and a destination
tick. A fleet says only its ETA. Nobody has read both on a real board yet.

**Whether `BUILDING_LEVELS` should travel on the snapshot.** Stating it in the client is the one
rules number this screen holds, and the argument for sending it is the argument ADR-069 made for the
tables. It is one byte on a wire that already carries six tables, and it was not added here because
nothing yet wants a building with a different number of levels from another.
