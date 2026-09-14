# ADR-113 — A move is chosen on the map

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner, in the place-sheet interaction handoff of 2026-09-14
(`design_handoff_place_sheet/`, frame `1b`) and the prompt that carries it.
**Supersedes:** — (replaces ADR-092's picker, amends ADR-063's row and ADR-077's entry points)

---

## Context

A move is given by picking a row from a list. The destination picker is a sheet against the bottom
of the map pane: one 44-pixel row per lane out of where the fleet stands, nearest first and then by
name (ADR-092), each saying whose the far end is and what is standing on it (ADR-063), with the lane
cost and the arrival tick on the right. Picking a row queues the order and closes the sheet.

**The list is drawn over a map that is already showing everything on it.** The lanes out of the
fleet's system are on the plane with their tick costs at their midpoints; the far ends are named,
owner-coloured, and wearing garrison badges that total the ships standing there. The picker restates
that in words, in a sheet that covers the bottom half of the thing it is restating — and the player
who has just tapped a fleet on the map is then looking away from it.

**And the row is an order.** One tap on a 44-pixel row in a list of six commits a fleet, with no
step between deciding and doing. It is editable until the lock (ADR-031), so the cost of a slip is
one more trip through the same sheet, but a slip is still the commonest thing that happens to a list
of near-identical rows under a thumb.

Since ADR-111 the fleets are on the place sheet and `MOVE ›` is the control that leads here, so the
picker is now reached from exactly one place — which is the moment to ask whether it should exist.

## Options considered

### A. Keep the picker and put an ETA on the map beside it

The map lights the reachable systems while the sheet is open; the sheet stays the way an order is
given. It is the smallest change and it leaves two places to read one answer, with the authoritative
one being the list — so the map's lighting is decoration.

### B. A mode on the map, with a confirm strip under it

Tapping `MOVE ›` collapses the sheet to a strip: a banner across the top of the pane says what is
being moved and from where, the reachable systems light with a pulsing ring and an ETA chip, the
lanes to them go blue and their dashes march toward the destination, and every other lane drops to a
hairline. Tapping a lit system *selects* it; the strip's header says what is there and its filled
`SEND` commits. The list survives as a two-column grid on the strip, for the player who would rather
read than aim.

It costs a mode — the one state on this screen a player can be stuck in — and it costs the map's
ordinary meaning for as long as it is on: no disc opens a place, no badge opens a sheet, no marker
opens anything.

### C. A mode with no list at all

The map only. It is the shortest strip and it fails the case the map is worst at: two systems close
together at a steep camera angle, where the chips overlap and the rings are ten pixels apart. The
list is what that player uses, and a mode that has no answer for them is a mode that has to be
escaped before the order can be given.

### D. Drag from the fleet to the destination

The gesture the mode is imitating. The map already owns the drag — it orbits the camera (ADR-017) —
and a second meaning for it would have to be disambiguated by where the press landed, on a badge
that is 16 pixels wide.

## Decision

**B.** A move is chosen on the map, and the list is the fallback rather than the path.

**Entering.** `MOVE ›` on a fleet's row in the place sheet; the digest's `MOVE FLT 1 | 10 SHIPS`;
the fleet's row in the rail's `ORDERS` while it is unordered; a marker of your own that has not
departed yet; and **a garrison badge with exactly one of your fleets under it**, which skips the
sheet. That last one is ADR-079's own argument one door further along: a badge totals ships, so a
place holding one fleet has exactly one thing a tap could mean, and a sheet between the finger and
the map would be a tap spent on a question with one answer. Several fleets is a real question and
the sheet is where it is asked.

**On entry** the sheet collapses — a sheet and this mode are alternatives rather than layers, in
both directions — the digest fades to 55% and records no hit at all, and the map changes meaning.

Seven things inside that had alternatives.

**1. The reach is one lane, because that is what the rules allow.** `Match::Validate` refuses any
destination that is not one lane from where the fleet stands (`NoLaneToDestination`), so the
handoff's *"plus multi-hop within the fleet's range if the rules allow"* resolves to the adjacent
systems and nothing else. **This is the one place the handoff describes a game that does not
exist**, and lighting a system the lock is certain to refuse is precisely what ADR-053 took off this
screen.

**2. Lighting a system is a SELECTION and sending it is an order.** The map lights, the strip's
header says what stands at the far end (ADR-063 — never a verdict, because the wire carries one only
for a fleet already flying), and the filled `SEND 10 SHIPS TO FAROE` is the one thing that commits.
It is the only filled control on the screen while the mode is on, which is what ADR-089 has always
asked and what the digest's fade makes true. Before a selection it is inert and reads `PICK A
DESTINATION`: a filled button that refuses a tap is worse than one that is plainly not ready.

**3. Everything on the map that is not a destination stops being a target.** An unreachable system,
a rival's garrison and a fleet marker are still drawn at their own ink — the mode hides nothing —
and none of them records a hit, so a tap on any of them falls through to the end of `HandleTap` and
leaves the mode. That is one rule where the handoff lists three (*the origin, empty map, `ESC`, or
`CANCEL`*), and it is the same rule: all three of the first are places that offer nothing.

**4. The banner is interface and the rings are world, and the split is forced.** The interface is
two renderers and each is one batch — every shape, then every glyph (ADR-014) — so a shape recorded
after the map's labels still lands under them. A banner drawn in the world pass would have the map's
own names showing through it; drawn in the interface pass, after the flush, it covers them. The
rings, the lanes and the ETA chips belong to the map for the opposite reason: they are attached to
projected positions, and the chip goes through the same label field that keeps the system names out
of each other's way (ADR-090).

**5. The rail keeps its links and following one leaves the mode.** The locks rail is a list of what
goes in at the lock, and changing your mind about which place you are looking at is a thing to be
able to do. What it must not do is open a sheet behind the strip, so `OpenPlace` leaves the mode —
the same trade `EnterMove` makes in the other direction.

**6. The clock stops on demand, and `--still` is the switch.** The ring breathes from 0.55 to full
on a 1.6-second period and the lanes' dashes march at 10 pixels a second, both as pure functions of
`m_animationSeconds`; a capture of either is a capture of whichever phase the shutter caught, and
`Build/Screenshot.ps1` waits two seconds and photographs whatever is there. `--still` holds the
clock at zero, where the pulse sits at its floor, and makes `Animating()` false so the page settles.
Every headless test already gets phase zero by never calling `Update`.

**7. `X` and not `×`, because the font does not have one.** `FontRenderer::GlyphOf` falls back to a
BLANK for a codepoint that was not baked, so the rail's take-back cell (ADR-112) drew nothing at all
until this was found. The alphabet is ADR-014's list baked by ADR-073 — printable ASCII plus `·`,
`–`, `‹`, `›`, `→` and `−` — and the handoff draws three characters outside it: `×`, `…` and `↓`.
The cell says `X`, the strip's header before a selection is `FLT 1 →` with nothing after it, and its
hint is `OR PICK FROM THE LIST`. **`FaceRuleTests::EveryStringTheScreensDrawIsInTheBakedAlphabet`
now asserts this over every string all five screens draw**, because the failure mode is a control
with nothing on it and the only other way to find out is to look at a capture. The one gain is that
`–` finally has a site: the `PLACES` row of a place with no order on it (ADR-014 baked it and
recorded that nothing used it).

## Consequences

**The order takes three taps where it took two**, and the third is the point: one to take the move
onto the map, one to light a destination, one to send it. A slip on the map now costs a tap instead
of a tick.

**`Panel::Destination` is gone and `ADR-092`'s sort moved with the list.** `ReachableFor` sorts by
ticks and then by name once, and the strip's rows and the map's chips are that one list drawn twice.

**The map has a second animation and the page has a reason to redraw while nothing is happening.**
`Animating()` was true only while a fleet was under way (ADR-055); it is now true whenever the mode
is on, which is a frame a second for as long as somebody is deciding. `--still` is what takes it
back.

**`ADR-079`'s open question is answered twice over.** The badge and the disc behaved differently at
the lock because they opened different things (ADR-111 made them open one sheet); and a badge over a
single fleet now skips that sheet entirely, which is the behaviour the badge was introduced to make
possible.

**Five captures are owed and none of them exists**: `01-move-mode.png`, `01-move-selected.png`, and
retakes of `01-main-page.png`, `01-orders-queued.png` and `06-at-lock.png`. They need a Windows
D3D12 run and `Design/UI/README.md` says so.

## What this changes elsewhere

- **Design/:** `UI/DESIGN-GUIDELINES.md` §Map (the mode) and §Components (the strip, the banner);
  `UI/SCREENS.md` 01 (the map's taps and the move); `UI/README.md` (the capture debt). Done in this
  commit.
- **Code:** `LockstepClient/MapRender.{h,cpp}` (`MoveTarget`, `MapHit::moveTarget`, the lanes, the
  rings, the chips, the origin outline); `LockstepClient/MainPage.{h,cpp}` (`MoveMode`,
  `MoveTargetSystem`, `BeginMove`/`SendMove`/`CancelMove`, `EnterMove`, `ExitMove`, `ReopenMove`,
  `ReachableFor`, `DrawMoveMode`, `Faded`, `AddHitUnlessMoving`, `SetStill`, `Escape`);
  `Lockstep/Lockstep.cpp` (`--still`).
- **Tests:** `LockstepTests/TapTests.cpp` (`MoveModeTapTests`, and the three-stage sweep in
  `FleetMoveTapTests`), `TouchTargetTests.cpp` (the mode's five new targets),
  `FaceRuleTests.cpp` (the mode's strings, and the baked alphabet).
- **AGENTS.md:** nothing.

## Open questions

**Whether the mode should survive a tap on the rail rather than being left by it.** Following a rail
row abandons a half-made move, which is the right answer for a player who changed their mind and the
wrong one for a player who wanted to check something first.

**Whether the strip should say what a destination would COST in ships.** ADR-063 forbids a verdict
because the wire carries one only for a fleet already flying; the strip says what is standing there
and leaves the arithmetic. A preview per candidate on `SnapshotFleet` is what would change that, and
it is the same open question ADR-063 left.

**Whether splitting a fleet belongs here.** Out of scope for this pass, and the hook is the banner's
`10 SHIPS`: it is the one number on the mode that would become a stepper.

**Whether `--still` should freeze the countdown too.** It holds the animation clock and not the
match's, so a capture taken with it still shows a different countdown every second — which is
usually what a capture wants and is occasionally the thing that makes two of them differ for no
reason.
