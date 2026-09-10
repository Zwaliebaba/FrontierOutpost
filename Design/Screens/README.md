# Handoff: Frontier Outpost — Main Page (option 1a "Ops console")

## Overview
The single main screen of Frontier Outpost, an asynchronous, tick-quantised space 4X (see `Design/space-4x-one-pager-v10.md`). It is the screen a player opens once or twice a day: read the tick digest, look at the map, adjust orders, answer a proposal, leave something in flight. Depicted state: quiet mid-match, tick 46 resolved, tick 47 locks in 02:14:09, 12 players, player is 4th.

## About the design files
`Frontier Outpost Main Page pixelfont.dc.html` (+ `support.js`) is the **design reference built in HTML**, not production code — set in an 8×8 pixel font (Press Start 2P stands in for the game's bitmap font). `Frontier Outpost Main Page 1a.dc.html` is the same layout in a proportional font, kept for spacing/hierarchy reference only. Recreate it in the game's actual UI stack (Unity UI Toolkit / Godot / web client — whatever the project uses) with its own patterns. `main-page-pixelfont.html` / `main-page-1a.html` are the same screens as self-contained single files. **`PROMPT.md` is the prompt to hand to Claude Code** to integrate this screen into the codebase.

## Fidelity
**High-fidelity.** Colors, type, spacing, and copy are final intent; match them closely. Map geometry is illustrative (a sample galaxy); the projection rules below are the spec.

## Frame
Fixed 1280×720 (tablet landscape). All colours are 8-bit RGBA. **Font: the game's single 8×8 fixed bitmap font.** Two sizes only: 8px (1×) for everything, 16px (2×) for the lock countdown in the top bar and the orders-rail footer. No letter-spacing, no weights; emphasis is done with colour (primary vs muted) and uppercase. Line-height 1.5 (12px per 8px line). Render at integer scale, no anti-aliasing; keep every text origin on whole pixels. The IBM Plex sizes/weights mentioned below describe the proportional reference file only — in the pixel-font build every one of them maps to 8px, except the 20px countdown → 16px.

Layout: CSS grid, rows `48px | 1fr`; the content row is columns `300px | 1fr | 330px` (digest rail · map · orders rail). Rails are separated from the map by `1px rgba(255,255,255,0.10)` lines.

## Design tokens
Backgrounds
- App / rails: `rgb(11,14,20)`
- Map pane: vertical gradient `rgb(8,10,16) → rgb(16,22,36) at 45% → rgb(11,14,20)`
- Card fill (fleet rows): `rgba(255,255,255,0.04)`; card border `rgba(255,255,255,0.10)`; dividers `rgba(255,255,255,0.07)`

Text
- Primary: `rgb(240,243,247)` · Body: `rgb(214,220,228)` · Muted: `rgba(214,220,228,0.55–0.6)`

Accents (owner colours double as semantic colours)
- You / accept / trade lane: `rgb(94,196,255)`
- Rival Halvorsen / warning / countdown / proposals: `rgb(255,196,87)`
- Rival Sorne / loss: `rgb(255,110,96)`
- Sealed region: `rgb(170,140,255)`
- Neutral / custodian systems: `rgba(214,220,228,0.45)`

Type scale (px / weight / family)
- Section labels: 11 / 400 / Mono, letter-spacing .1em, uppercase, muted
- Event title / card title: 13 / 500 / Sans, primary
- Event detail / card detail: 12 (digest) or 11 (orders) / 400 / Mono, muted
- Countdown (top bar): 20 / 600 / Mono, amber, letter-spacing .04em
- Buttons: 11–12 / 600 / Mono, uppercase
- Map system names: 11 Sans (capitals 600 uppercase); lane costs & overlays 9 Mono

Radii: 3px everywhere. Spacing: 14px rail padding, 10px card padding, 6–8px between cards.

## Screen anatomy

### 1. Top bar (48px)
Left: `FRONTIER OUTPOST` (12 Mono 600, .14em) · `MATCH 0419 · DAY 12 / 21 · ENDS 22 SEP 18:00Z` (muted).
Right, separated by 1px vertical dividers:
- **Lock countdown**: label `TICK 47 LOCKS IN` (muted) + `02:14:09` (20px amber). Counts down live to the next fixed UTC tick.
- **Score & placement**: `SCORE 1,284` · outlined chip `4TH / 12` · `LEADER HALVORSEN 1,610`. Leader is always visible (design rule).
- **Replay tick 46**: outlined button with ▶ glyph. Opens a step-through of the last resolved tick (phases 1–6).

### 2. Digest rail (300px, left) — the primary read
Header `DIGEST · TICK 46` / `7 EVENTS`. Then one row per event, sorted by consequence, each: 8px colour dot + title (13/500) + one Mono detail line. The top event carries a 2px left border in its colour. Events shown, in order:
1. amber — **Contact · HALVORSEN at Kepler-Reach** — "Outpost present. Their fleet ETA T47. Ours ETA T47."
2. blue — **Proposal · HALVORSEN → you** — "Open lane Orune–Kepler-Reach · +6/tick · 3 ticks left"
3. red — **Lane cancelled: system lost** — "Tamsin–Pell. PELL captured by SORNE (siege T44–45)" (digest must distinguish *cancelled by partner* vs *system lost*)
4. neutral — **OKONKWO custodian since T43** — "Garrisons −3. Narth reachable in 2 ticks."
5. purple — **Sealed region opens T60** — "14 ticks. Shortest path from Vesk: 9."
6. neutral — **Production +38 · research +4** — "Lane income foregone: 12/tick. Shipyard Idris idle."
7. neutral — **Proposal ignored · SORNE** — "Share scouting, sent T42. No answer in 4 ticks."
Copy tone: terse ops console, no prose. One digest per tick, never per event.

### 3. Map (centre) — commitments as overlays, drawn in perspective
The galaxy is a graph (systems = nodes, lanes = edges with an integer tick cost). Render it as a tilted ground plane:
- Star field: ~30 tiny dots `rgba(214,220,228,0.55)`, r 0.7–1.2, unprojected background.
- Projection (design space 800×560 → screen): for a node at `(x, y)`, `d = y/560`, `s = 0.5 + 0.65·d`, `sx = 400 + (x−400)·s`, `sy = 60 + 440·(0.3·d + 0.7·d²)`. Node size, label size and stem height scale with `s`. Use `preserveAspectRatio: meet` (letterbox, never crop). **Superseded 2026-09-11 by ADR-017 — the client uses a perspective orbit camera instead. This curve is what the reference was drawn with, and is kept because it is what the sample geometry's coordinates were measured against; it is not what the game renders. Labels are 8px at every distance either way.**
- Ground grid: lines of constant x and constant y projected, `rgba(94,196,255,0.07)`, 1px; a soft radial glow `rgba(94,196,255,0.10)→0` near the horizon.
- Lanes: on the ground plane. Neutral `rgba(214,220,228,0.28)` 1.2px; **trade lane** 2.5px owner blue; **proposed lane** 2px blue dashed `4 5`. Tick cost as 9px Mono at the midpoint.
- Systems: ground shadow ellipse (owner colour @ .22 alpha, rx = 2.2r, ry = .9r), a 1px stem rising `20·s` px (`30·s` for capitals), then the node dot (r = 4.5·s·1.15; capitals 6·s·1.15) with a soft halo on capitals. Contested system: 1px ring. Custodian: dashed ring `2 3` + `CUSTODIAN T43` label below ground point. Captured: red `CAPTURED T45` label.
- Fleets in transit: an arrowhead hovering 14px above the lane at its progress fraction, with a stem, labelled `FLT 3 · ETA T47` (yours, blue) / `HALVORSEN · ETA T47` (rival, amber). Fleets are visible in transit with tick-ETA once departed.
- Sealed region: projected dashed ellipse (r 62·s, ry = .42·rx) filled `rgba(170,140,255,0.08)`, a second faint ellipse lifted 26·s px for volume, three raised site pins, label `SEALED · OPENS T60`.
- Overlays: top-right `12 PLAYERS · 41 SYSTEMS · 0 UNCLAIMED`; bottom-left legend (YOU · HALVORSEN · SORNE · proposed lane · trade lane), 10px Mono .08em.

### 4. Orders rail (330px, right) — three columns, lock together
Header `ORDERS · TICK 47` / `UNLOCKED` (amber). Sections stacked (on tablet the "three columns" become three stacked groups):
- **FLEETS · 2** — cards: `FLT 3 · 14 ships` → Kepler-Reach, "in transit · ETA T47", "preview: 14 v 11 (+def) · 6 left" (deterministic combat preview from visible info); `FLT 1 · 9 ships` HOLD Vesk, "incumbent · defender bonus", `change ›`.
- **BUILDS · 38 AVAILABLE** — `Shipyard · Idris` −20 · ready T49 [BUILD]; `Mining station · Tamsin` −15 · +4/tick [BUILD]; **`Trade lane with HALVORSEN`** Orune–Kepler-Reach · +6/tick · both owners — dashed amber border, outlined amber **[PROPOSE]** where Build would be. Diplomacy has no tab; the lane is a building with two owners.
- **PROPOSALS · 1 OPEN** — highlighted card (blue border, `rgba(94,196,255,0.06)` fill): `HALVORSEN · open lane` / `3 TICKS` amber, "Orune–Kepler-Reach · +6/tick each · re-validated at lock", buttons [ACCEPT] (filled blue, dark text) [DECLINE] (outlined).
Footer: `ALL THREE LOCK TOGETHER` · `02:14:09` (amber 600).

Buttons: filled = `rgb(94,196,255)` bg / `rgb(11,14,20)` text; outlined = 1px border in accent or `rgba(255,255,255,0.2)`; hover = `rgba(255,255,255,0.08)` fill.

## Interactions & behaviour
- Countdown ticks every second toward the next fixed UTC tick; at 0 the orders rail flips `UNLOCKED → LOCKED`, the tick resolves, a new digest replaces the old, and `Replay tick N` advances.
- Orders (fleet moves, builds, proposals) are editable any time until lock; edits are local until then. Acting on a proposal is itself an order that locks with the others.
- Tapping a digest event focuses the related node/lane on the map. Tapping a system opens its build list; tapping a fleet opens a destination picker on the map (lane-constrained, shows tick ETA).
- Trade-lane rows are greyed with *Propose* until the neighbour accepts; then they render like any other building (income row).
- Proposals show a live tick countdown; an unanswered proposal at 4 ticks is reported to the proposer as *ignored*.
- Map: pan/zoom. ~~Perspective is fixed (no free camera).~~ **Amended 2026-09-11 by ADR-017: the map has a real perspective camera that orbits the galaxy — drag to yaw and to raise or lower the eye.** The projection specified above is no longer what the client uses; it was an authored curve and could not be looked at from another angle. The opening view is aimed to approximate the framing drawn here, but a lens does not reproduce a drawing, so it is close rather than identical. Pan and zoom are still not implemented. Optional slow pulse (`fo-pulse`, opacity .35↔1) on fleets with ETA = next tick.
- No free text anywhere in v1.

## State
- `match { id, day, totalDays, endsAt, tick, nextLockAt }`
- `player { score, placement, playerCount, leader { name, score } }`
- `digest[tick] : Event[] { kind, severity, title, detail, refs (systems/lanes/fleets) }`
- `graph { systems[] { id, name, owner, pos, flags: capital|contested|custodianSince|capturedAt }, lanes[] { a, b, cost, kind: none|trade|proposed } }`
- `fleets[] { id, owner, ships, from, to, progress, eta, order: move|hold }`
- `orders { fleets[], builds[], proposals[] , locked: bool }`
- `proposals[] { from, to, type, terms, ticksLeft, conditionalOrder }`
- `region { center, opensAt, sites[] }`

## Assets
None. Only two glyphs: the ▶ replay triangle (CSS border triangle) and the map, which is generated geometry. Fonts from Google Fonts (IBM Plex Sans / Mono).

## Files
- `Frontier Outpost Main Page pixelfont.dc.html` — the screen in the 8×8 font (open with `support.js` alongside)
- `main-page-pixelfont.html` — self-contained single-file version
- `PROMPT.md` — Claude Code integration prompt
- `Frontier Outpost Main Page 1a.dc.html` — proportional-font reference
- `support.js` — runtime required by the .dc.html file
- `main-page-1a.html` — self-contained proportional-font reference
