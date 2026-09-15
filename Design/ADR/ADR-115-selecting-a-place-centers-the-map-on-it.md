# ADR-115 — Selecting a place centres the map on it, by panning and not by zooming

**Status:** Accepted

**Date:** 2026-09-15
**Decided by:** Owner decision, 2026-09-15. The owner reported that selecting a location does not
bring the camera to it, and chose the scope when asked: *any system tap on a known system*.
**Supersedes:** — (revises ADR-090's "pan is still absent" clause)

---

## Context

Selecting a place on this screen has never moved the camera, and the owner's report is that it
should. What a selection buys today is two pieces of drawing: a `TEXT_PRIMARY` ring at three times
the ball's radius, and the `MAP - FOCUS: PELL` caption in the corner of the pane. The camera does
not read the selection at all. `MeasureContent` measures the galaxy's extent once per snapshot and
stores its midpoint; `BeginMap` re-aims at that midpoint on every frame. A system at the rim of the
plane stays at the rim of the pane however hard it is selected.

That is not a regression. ADR-017 framed the whole galaxy on purpose, ADR-090 recorded the absence
in as many words — *"Pan is still absent and deliberately so — the map frames the whole galaxy and
ADR-017's argument for that is unchanged"* — and `DESIGN-GUIDELINES.md` §Map ends the camera line
with "No pan." The thing that did centre on a click was the MVP-01 slice's follow camera, where the
ship stayed centred and space scrolled under it (ADR-003); that was deleted with the slice on
2026-09-10 (ADR-015), five days before the map had a focus to select.

What has changed since is the screen around it. ADR-112 made a sheet the only door an order goes
through and ADR-113 turned the locks rail into a list of orders and then a list of places, so the
rail is now full of rows naming systems. A row that names a place and leaves the eye to hunt for it
is a link that does not navigate, and that is what made the absence worth paying for.

Five things bind:

- **The framing distance is solved from the whole galaxy on every frame** (ADR-017): the pane, the
  graph and the zoom all feed it. So a `SetTarget` made when a tap lands is gone by the next
  present, and the aim has to be state rather than a call.
- **The zoom is the player's**, a factor of 0.6x to 2.5x on the authored framing (ADR-090).
- **The tap that selects a place is usually the tap that opens a sheet over it** (ADR-112), and a
  sheet is anchored to the bottom of the pane and takes at most half of it (ADR-052).
- **The map must lay out identically twice for one state** (ADR-047, ADR-090): the idle throttle
  redraws from scratch and a screenshot has to be reproducible.
- **`AtAuthoredFraming` is the whole of the way back** (ADR-090): it is what decides whether the
  `RESET` chip is on the screen at all.

## Options considered

### A. Leave it: a selection is a ring and a caption

Costs nothing and keeps every measured figure in the design record exactly as taken.

Rejected because it is the reported defect. It also leaves ADR-113's rail half-built: a row that
names Kepler-Reach, on a screen where Kepler-Reach is 300 pixels from where you are looking, has
told you a name and not taken you anywhere.

### B. Centre and zoom in

Aim at the system and pull the eye closer, so that selecting a place also magnifies its
neighbourhood.

Rejected. The zoom is a factor on the authored framing and it belongs to the player (ADR-090); a
tap that moved it would fight the wheel and leave nobody able to say what magnification the map is
at. It also makes the selection destructive in a way A is not: the way back from a pan is one chip,
the way back from a pan *and* a zoom is the same chip pretending the two were one gesture.

### C. Centre only — pan the aim, leave the fitted distance alone

Aim the camera at the selected system's ground point and go on solving the distance from the whole
galaxy. The picture slides; nothing changes size.

Accepted. It is the smallest thing that answers the report, and it keeps the two controls the
player already has meaning exactly what they meant.

### D. Centre, and ease the camera across

C, with the aim interpolated over a few frames so the map slides rather than cuts.

Rejected for now, and not on taste. There is no easing anywhere in this client and adding it here
means adding a clock to the framing: `Animating()` would have to report a moving camera so the idle
throttle keeps drawing (ADR-047), and a map that lays out differently on two frames for one state
breaks the property every screenshot depends on. It is worth doing and it is a decision of its own.

## Decision

**A selection centres the camera on the selected system, by panning the aim.** `MapView` holds a
ground point to centre and whether it is centred on one at all; `FrameContent` aims at it instead
of the galaxy's midpoint, and goes on solving the distance from the galaxy's extent — so centring a
place never changes the scale the map is drawn at, and the zoom stays the only thing that does.

**The aim is state on `MapView`, not a call on the camera.** `FrameContent` re-aims from scratch
every frame, so a target set when the tap landed would survive exactly one present.

**The pan is a generic operation on the camera.** `OrbitCamera::PanPixels` moves the eye and the
target together along the view basis, by a number of screen pixels converted at the target's own
depth. Because the offset is perpendicular to the view direction it drops out of the depth the
perspective divide uses, so the slide is **exact** for a point at the target and not merely close:
measured, a 175-pixel lift moves the aimed point 175.000 pixels at every yaw and pitch tried.

**Every focus goes through one door.** `MainPage::FocusOn` sets the focused system and aims the
camera, and it is the only thing that writes `m_focusedSystem`: the map's disc, the garrison badge
beside it, a fleet marker already under way, a locks-rail row, a digest event's focus chip, and
entering a move mode all call it. "The camera is centred on what is focused" is one rule, and a
screen with it written at five call sites is a screen where four of them are eventually wrong.

**An index the graph does not have focuses nothing**, and frames the whole galaxy again. The graph
is fogged and a position means a system only while this snapshot is the current one (ADR-057), so
an index arriving from a card, a row or a stale frame is a question this answers rather than stores.
That also retires a second bounds check that used to sit in the tap handler.

**A centred system is lifted clear of the sheet.** `FOCUS_LIFT_PIXELS` is
`(SHEET_MAP_SHARE + SHEET_MARGIN) / 2`, which is 175: every sheet on this pane is anchored to its
bottom and none is taller than the share plus the margin under it, so half of what a sheet takes is
what moves the pane's middle to the middle of the map still showing. **It is measured against the
SHARE and not against the sheet drawn this frame**, which is a rule rather than an approximation —
a place sheet is as tall as what is on the place, so a lift read off the sheet itself would re-aim
the camera the tick a fleet arrived and gave it a row. The lift applies only when a sheet is open
and only when the camera is aimed, so a map with nothing selected frames exactly as it always did.

**`AtAuthoredFraming` counts the aim, and `RESET` is the way back.** A camera moved off the middle
of the galaxy by a tap is one the player has to be able to undo, and that predicate is what puts
the chip on the screen to do it. `ResetView` clears the aim and **leaves the focus alone**: the chip
is about where the camera is, not about what the screen is pointed at.

**A snapshot takes the centring with it**, because it already takes the focus (ADR-057).

## Consequences

**What this makes easy.** Navigating by name. Every row ADR-113 put on the rail and every focus chip
ADR-020 put on a digest card is now a link that goes somewhere, and the far side of the plane is
reachable without orbiting to find it. ADR-112's comment about a tap putting Kepler-Reach *under
your eye* is literally true for the first time.

**The map cuts rather than slides.** There is no easing (option D), so a selection moves the camera
between one frame and the next. Opening or closing a sheet after a centring moves it another 175
pixels, in one frame, for the same reason.

**Centring a rim system pushes much of the galaxy out of the pane.** The distance still fits the
whole galaxy about *its* middle, so aiming at the edge of the plane leaves roughly half the framing
hanging off the far side. That is what centring means at a fixed framing rather than a defect of
this one, and it is the strongest argument for option B that this ADR does not take.

**`RESET` is on the screen far more often.** It used to appear only after a wheel notch or a drag;
it now appears after any tap on a system. That is the chip doing its job — the camera really has
moved — but the map pane's chrome is no longer almost always empty.

**The lift is a constant, so a short sheet over-lifts.** A two-row place sheet takes well under its
share, and the system it is about sits above the true middle of the band that is showing rather than
in it. Clear of the sheet is the requirement; centred in the remainder is the nicety given up to
keep the camera from moving when a sheet grows a row.

**One edge case answers a tap where it used to ignore one.** A digest card's focus chip carries
whatever the event pointed at and `AddHitUnlessMoving` records it unguarded, so a chip about an
event with no system reaches the handler with `EventRefs::NONE`. `Action::FocusSystem` used to
return having done nothing at all; it now closes the panel and frames the whole galaxy, which is
what every other focus does and is the position this screen already takes -- a control that
silently ignores you is the defect it has been bitten by twice (ADR-058). `Action::OpenSystem` is
unchanged: it already stored the bad index and opened a sheet about nothing.

**A tap-driven test can no longer find a control on the map by sweeping for it**, and six had to
stop. `Headless.h`'s sweeps walk an 8-pixel grid and rely on the board being the same board at
every step; a tap that centres the camera lays the map out again, so a sweep looking for one
particular control — the garrison badge over a single standing fleet — is searching a board that
moves between its steps. It is not the claim that broke but the search: every test that finds its
control in the recorded hit list passed unchanged, and the six now press a recorded rectangle the
way `OpenPlaceSheet` already did. **That is a real loss of coverage**, because a blind sweep also
proved the control was reachable at a pixel a finger could land on, and pressing the middle of its
rectangle takes that on trust. The sweeps that look for *any* owned system, and the one that sweeps
to prove a locked sheet takes nothing, are left alone: the first is satisfied by the first tap that
lands, and the second can only get weaker rather than wrong.

**`MapView` grew two members**, so a `MapView` copied while centred carries the aim. Nothing copies
one, which is what ADR-090 said of the zoom and is still true.

**`FrameContent` grew a fourth parameter** and `MapFrame` a field. The map is told a number of
pixels rather than which sheet is open, for the reason `sheetOpen` is a bool: the map does not know
what a panel is.

## What this changes elsewhere

- **Code:** `NeuronClient/OrbitCamera.{h,cpp}` gains `PanPixels`. `LockstepClient/MapView.h` gains
  the aim, `AimAt`, `AimAtContent`, `Aimed`, a lift parameter on `FrameContent`, and the aim term in
  `AtAuthoredFraming` and `ResetView`. `LockstepClient/MapRender.{h,cpp}` carries `aimLiftPixels`
  through to the framing. `LockstepClient/MainPage.h` declares `FocusOn` and `FOCUS_LIFT_PIXELS`;
  `MainPageMap.cpp` implements it and fills the field; `MainPageInput.cpp`, `MainPage.cpp` and
  `MainPageMove.cpp` route their focus assignments through it. Done in this commit.
- **Design/:** `DESIGN-GUIDELINES.md` §Map loses "No pan." and says what a selection does.
  ADR-090's status line records that this revises its pan clause; nothing else in it changes, because
  an Accepted ADR is immutable except for that line (`Design/README.md` §4).
- **AGENTS.md:** nothing. This adds no banned pattern and no new invariant about how code is written.

## Verification

**The arithmetic and the suites are measured; the screen is not, and that is the gap in this
record.** The session that wrote this had no Windows toolchain and no GPU, so nothing here was
photographed and nothing was run locally beyond what compiles without `<windows.h>`; the four MSVC
suites ran on CI afterwards and are reported below. What follows is what was actually checked and
how.

`NeuronClient/OrbitCamera.cpp` and `LockstepClient/MapView.h` compile and run standalone — neither
reaches `<windows.h>` — so both were built with g++ 14 at `-Wall -Wextra -Werror` against a harness
that drives the shipping code rather than a copy of its arithmetic. Measured on 2026-09-15:

- **The pan is exact at every angle.** Across four yaws (0, 0.9, -2.2, 4.4 rad) and three pitches
  (0.2, 0.62, 1.3 rad), a `PanPixels(0, -175)` moves the aimed point 175.000 pixels up, moves it
  0.000 pixels sideways, and changes its depth by at most 0.0001 world units. A `PanPixels(60, 0)`
  moves it 60.000 pixels right.
- **A centred system lands in the middle of the pane.** In the 620x676 map pane the galaxy's midpoint
  projects to x 710.000, and four systems taken from across the design space — the middle, Idris on
  the left, the far corner and one on the top edge — each project to (710.000, 382.000) once aimed,
  which is the pane's centre to a hundredth of a pixel.
- **The lift clears the sheet.** With the lift applied each of those four projects to y 207.000. A
  sheet at its full share has its top edge at y 370, and the pane starts at y 44, so the system sits
  in the middle of the band that is showing with 163 pixels of clearance.
- **Centring is not a zoom.** The framing distance is 1463.234 world units aimed or not, lifted or
  not, at every one of those systems.
- **The way back works.** A fresh `MapView` reports `AtAuthoredFraming`; one that has been aimed does
  not; `ResetView` and `AimAtContent` each restore it.

**The suites then ran on CI, and they are what found the cost above.** Run 103 on this branch built
`Debug|x64` clean and reported 663 of 669 passing. All six new methods passed —
`NeuronClientTests::OrbitCameraTests::APanSlidesThePictureByTheseExactPixels` and
`APanTurnsNothingAndKeepsTheDistance`, and `MapCameraTapTests::ATapOnASystemCentersTheCameraOnIt`,
`ACenteredSystemIsNotLeftUnderTheSheet`, `ATickFramesTheWholeGalaxyAgain` and
`ResetComesBackFromACenteringToo` — which is the pair of claims the standalone harness cannot make:
that a tap reaches the camera at all, and that the sheet the page actually draws is the one the lift
clears.

**The six failures were every `MoveModeTapTests` method that entered the mode by sweeping the map
pane**, each stopping at its own "no move to ..." guard. Every method that finds its control in the
hit list passed, which is what identified the search rather than the control as the thing that
broke. They press a recorded rectangle now, through `EnterMoveThrough`, and the badge test still
names the door it goes through so that what it claims about the badge stays what it claims.

`Build/CheckFormat.py` (clang-format 18; CI pins 22) reports 181 files and 0 unformatted.
`Build/CheckProjectFiles.py` output is byte-identical to the same script's output on the parent
commit. `clang-tidy` 18 with this tree's configuration reports nothing on either changed file.

**What is still unphotographed is the screen**, and that is what a build session should do next:
every figure above is either arithmetic or a test result, and none of them says the centring reads
well to a player — how far the map jumps, whether the lift looks deliberate or looks like a
mistake, and whether `RESET` appearing after every system tap is chrome this pane can carry.

## Open questions

**Easing** — option D, and the question is the clock it needs rather than the interpolation.
`Animating()` would have to report a camera in motion so the idle throttle keeps drawing, and the
map would lay out differently on two frames for one state, which is the property ADR-090's label
pass was made deterministic to protect. Worth doing; worth its own ADR.

**Whether a rail row should move the camera as far as a map tap does.** Both go through `FocusOn`
today, which is the consistent answer and may be the wrong one: a tap on a disc is a gesture aimed
at a place on the screen, and a tap on a row is a gesture aimed at a name. Nothing has been
photographed either way.

**Whether the lift should read the sheet actually drawn.** It cannot today: the sheet is laid out
after the map pass, so its height is not known when the camera is framed. Hoisting the layout above
the map would make the lift exact and make the camera move when a sheet grows, and the trade has
not been measured.

**Whether `RESET` appearing after every system tap is too much chrome**, now that a selection moves
the camera. The chip was designed for a pane that was almost always bare (ADR-090).

**Whether centring wants a zoom for a dense cluster.** Option B was rejected as a rule for every
selection; it was not asked whether a *second* tap on an already-centred system should magnify it,
which is a different control and would leave the wheel's meaning intact.
