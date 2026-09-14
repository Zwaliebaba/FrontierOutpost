# ADR-090 - The map has a zoom, a way back, and labels that keep out of the way

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 2.5 at the owner's instruction to work the plan.
**Supersedes:** - (finishes ADR-017's camera)

---

## Context

ADR-017 gave the map a real orbit camera and ADR-009 banked wheel notches and pinch steps into
`PointerInput::TakeZoomSteps`. Four things were left where they landed:

- **Nothing read the zoom.** ADR-080 taught the digest column to, for scrolling; the camera the
  gesture was banked for still ignored it.
- **`MapView::ResetView` existed and no control reached it.** Once a player had orbited the map
  there was no way back to the authored framing, and finding it by eye is not a thing to ask.
- **A fleet marker and a system marker are the same blue and differ only by size.**
  `Design/UI/README.md` measures them at 13-18px and about 9.
- **Labels overprint.** `FLT 1 · ETA T2` crosses the Xander lane in `01-fleet-under-way.png`, and two
  systems near each other put their names in the same place.

## Decision

**The zoom is a factor on the authored framing, not a distance.** `FrameContent` recomputes the
distance from the galaxy's extent on every frame -- a different pane, graph or aspect all change it
-- so a zoom kept as a distance would mean something different every time the content did. Kept as a
factor, `1.0` is always exactly what the map opens at, and the clamp UI-01 asked for reads as what it
is: 2.5x in, 0.6x out. Twelve percent a notch takes about eight of them to cross the range.

**One banked count, two meanings, and the pointer picks.** `HandleZoom` takes the position: over the
digest column a notch is a list (ADR-080), over the map it is a camera, over the locks rail it is
nothing. The sign is flipped for the list and kept for the camera, because away-from-the-player is
*out* and that is what the count was banked as.

**`RESET` is drawn only when the camera is somewhere other than where the map opened.** An outlined
chip immediately after `MAP - FOCUS: PELL`, sharing that string's composition (`FocusLine` moved into
`MapRender.h`) so the two cannot disagree about how wide it is. A control that would do nothing is
left off the screen rather than drawn dim: the map pane has no chrome, so one chip appearing is
itself the signal that something has been moved.

**A fleet is a triangle and a system is a disc, and the legend says so.** The map already drew a
fleet as an arrowhead; what it did not do was teach that. The `FLEET UNDER WAY` legend entry wore the
route's dashes -- the thing the fleet travels along rather than the fleet -- and is now the same
arrowhead in the same blue.

**Labels are placed by one greedy upward pass, and it gives up rather than searching.** `LabelField`
collects the lane segments as they are drawn and every label box as it is placed; each new label is
nudged up in 12px steps, at most three, until it clears both. **It is deterministic by
construction** -- the same depth-sorted order, fixed steps, no iteration to a fixed point -- because
the map is redrawn from scratch every frame and must lay out identically twice for one state, which
both a screenshot and an idle redraw depend on (ADR-047). A label that cannot be cleared in three
steps is drawn where it was: a name 36 pixels from the thing it names has stopped being that thing's
label, and a wrong label beats a lost one.

**A garrison badge rides on its label's baseline and joins the field.** The two are read as one thing
(ADR-079), so a label that moved up and a badge that did not would come apart on exactly the crowded
boards that made it move; and the badge is drawn ink competing for the same strip, so the next
label avoids it too.

## Consequences

- **The camera is now three-dimensional in the player's hands**: orbit by drag, zoom by wheel or
  pinch, reset by chip. Pan is still absent and deliberately so -- the map frames the whole galaxy
  and ADR-017's argument for that is unchanged.
- **The label pass is O(labels x (labels + lanes)) per frame.** On the reference board that is about
  20 x 45, which is nothing; it would matter on a galaxy an order of magnitude bigger, and the
  fixed three-step cap is what stops it becoming a search.
- **Labels can now be one to three lines above where they used to be**, which makes the map's
  vertical rhythm less regular. That is the trade for none of them being unreadable.
- `MapView` grew a member, so a `MapView` copied mid-zoom carries the zoom. Nothing copies one.
- **`near` and `far` are empty macros in `minwindef.h`**, which `NeuronCore.h` does not suppress, and
  a local called either one vanishes into a syntax error. The slab clip inside `LabelField` names
  them `entering` and `leaving` and says why.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Map, `SCREENS.md` 01, `README.md` item 6 (the zoom sentence
  goes). Done in this commit.
- **Code:** `LockstepClient/MapView.h` (the zoom factor, `Zoom`, `AtAuthoredFraming`, `ResetView`),
  `LockstepClient/MapRender.{h,cpp}` (`FocusLine`, `LabelField`, the legend's arrowhead, the badge
  on the placed baseline), `LockstepClient/MainPage.{h,cpp}` (`HandleZoom` routes by pane,
  `Action::ResetCamera`, the chip). Done, built and photographed.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `MapCameraTapTests` pins that a notch over the map moves the camera and reports no
change at either end of the range, that a notch over the locks rail does nothing to either pane, that
`RESET` appears only once the camera has moved and that sweeping the top of the map pane finds it,
and that a drag still orbits and reset undoes that too.
`AWheelOverTheDigestScrollsItAndOverTheMapDoesNot` was corrected: it asserted that `HandleZoom`
returned false over the map, which conflated "the digest did not scroll" with "nothing happened" --
it now asserts what the digest did and that the map took the notch. All 150 methods pass.

Photographed on 2026-09-14 at T8: no `RESET` chip at the authored framing, which is the half that is
visible without a tap.

## Open questions

**Whether the label pass should be able to move a label sideways or down.** Up only is what UI-01
asked for and it is what a label wants when the thing below it is the node it belongs to. Two systems
side by side at the same height still collide after three steps.

**What a pinch does on a device with no wheel.** It banks into the same count, so it zooms the pane
under it -- which for the digest column means a pinch scrolls a list. Nothing has tested that,
because nothing in this project has a touchscreen yet, and UI-01 3.8's answer (touch) means it will
need to be.
