# Design guidelines — LockStep: Universe client

## Frame
- 1280×720 logical pixels, fixed. Letterbox on other aspect ratios; never scale non-integer.
- Grid on the main page: rows `44 | fill`; columns `400 | fill | 260` (digest · map · locks). 1px separators `rgba(255,255,255,0.10)`.
- Rail padding 14px. Card padding 8px. 6px between cards. Everything on a 2px grid.

## Font
One 8×8 fixed bitmap font (reference renders use *Press Start 2P* as stand-in).
- 1× = 8px for all text. Line height 12px (1.5).
- 2× = 16px **only** for: the lock countdown (top bar), the share-card headline.
- No weights, no italics, no letter-spacing. Emphasis = colour and case. Labels/headers uppercase; sentences mixed case.
- Text origins on integer pixels; no sub-pixel positioning, no anti-aliasing.

## Palette (8-bit RGBA, exhaustive)
UI
- `rgb(11,14,20)` background / ink
- `rgba(255,255,255,0.10)` structural line · `rgba(255,255,255,0.07)` row divider · `rgba(255,255,255,0.25)` outlined-button border
- `rgba(255,255,255,0.04)` card fill
- `rgb(240,243,247)` primary text · `rgb(214,220,228)` body · `rgba(214,220,228,0.6)` muted · `rgba(214,220,228,0.35)` disabled/dim
- Map ground: vertical gradient `rgb(8,10,16) → rgb(16,22,36) @45% → rgb(11,14,20)`; grid `rgba(94,196,255,0.07)`; horizon glow `rgba(94,196,255,0.10)→0`

Semantic / owner (ADR-027: you are always blue)
- `rgb(94,196,255)` **you** · primary action · trade lane · accept
- `rgb(255,196,87)` **rival Halvorsen** · warning · countdown · proposals · contact
- `rgb(255,110,96)` **rival Sorne** · loss · captured · refused
- `rgba(214,220,228,0.45)` neutral / custodian
- `rgb(170,140,255)` the Fallow (sealed region)
Other seats take their ADR-027 colours; names are on every node so colour is never the only channel.

## Components
- **Button, filled** — bg `you` blue, text ink, 5×8px padding. One per event max (the primary action).
- **Button, outlined** — 1px `rgba(255,255,255,0.25)`, text primary. Warning variant: 1px amber / amber text (e.g. PROPOSE).
- **Chip** — 1px border in semantic colour, 1×5px padding, same colour text (`3 TICKS`, `4/12`).
- **Event row** — 8px owner square + title (primary) + detail lines (muted, indented 16px) + action row (indented 16px). Top divider `0.07`. Highlighted event: left border 2px amber + fill `rgba(255,196,87,0.06)`.
- **Actor card** — an event row whose title is the player (`HALVORSEN · LEADER 1,610`), listing his events as lines, with one verdict box and one action row.
- **Verdict box** — 1px amber border; line 1 amber `FLT3 ARRIVES T47 · YOU LOSE`; line 2 muted with the numbers. Always states whose ships remain.
- **Sheet** — a panel over the map pane (ADR-052): full pane width less a 12px margin, anchored to the bottom, ink bg, 1px `0.10` border. 36px header (title left, `X` right, the whole 36×36 corner tappable); rows **44px**, which is the top bar's height and the smallest reliable touch target; 40px full-width `CANCEL` bar under the list. A row is an 8px owner square + title (primary) + optional second line (muted, under the title) + optional right-aligned status (detail). **Row height never varies with content** — one line centres, two sit either side of the middle. Six rows maximum; a seventh is reported in a 24px muted row, never dropped.
- **Dialog** — 440px, ink bg, 1px border in state colour (blue welcome / amber warning / red refusal / neutral), 18px padding, title in state colour, muted body, right-aligned actions. Sits on `rgba(6,8,12,0.74)` scrim over the live page.
- **Tabs (unread ticks)** — equal-width; active filled blue; inactive outlined muted.
- **Locks list row** — label primary left, status right (RED `LOSE`, AMB `PROPOSE`/`3 TICKS`, muted otherwise).

## Copy
- Ops-console terse: no articles where they cost nothing, numbers first. `Pell lost to Sorne · T45`.
- The digest opens with **SINCE YOU LOOKED · Tn > Tm** and a four-cell delta (`-1 SYSTEM · 1 CONTACT · 2 PROPOSALS · 1 LANE LOST`) when ≥1 tick is unread.
- Consequence order: system lost > contact at a system your fleet reaches next tick > proposal received > custodian/absence > region/timer > income > silence.
- Group by actor when one player produces ≥2 events in the window; rank the card by its worst event; show `LEADER` when he is.
- Combat preview is a **verdict**: `YOU LOSE` / `YOU WIN` / `HOLD`, then `You arrive N. He holds M +def. X of his remain, Y of yours.` Never a bare `A v B`.
- Every event carries its own actions; nothing is only in a menu. Signals are a fixed vocabulary: `HOLD FIRE`, `SHARE SCOUTING`, `PROPOSE LANE`, `WITHDRAW`. No free text.
- System count and player count come from the match, never hard-coded (61 systems / 12 players in the reference).

## Map
Graph drawn on a tilted plane, design space 800×560.

**The projection below is how the mockups were rendered, not the spec** (ADR-034). The built map has
a real orbit camera — an eye position, a field of view, a perspective divide — which the player can
drag and zoom, and which the star field projects through as directions on a sphere (ADR-017,
ADR-032). The authored curve was what ADR-016 had; ADR-017 replaced it *because* it could not move
the viewpoint. Implement against `Neuron::OrbitCamera`, framed by default to sit close to the view
these PNGs show.

```
d  = y / 560                        ← the mockups' curve, kept for provenance
s  = 0.5 + 0.65·d
sx = 400 + (x − 400)·s
sy = 60 + 440·(0.3·d + 0.7·d²)
```

The consequence to expect: the map pane will not be pixel-identical to its PNG once the camera
moves. Every other pane is fixed and can be compared exactly.

- Grid lines of constant x / y projected; stars unprojected behind — **the star field is a sphere of
  directions projected through the same camera** (ADR-032), not a 2D layer that slides.
- Lanes on the plane: neutral `rgba(214,220,228,0.28)` 1.2px; your trade lane blue 2.5px; proposed lane amber dashed `4 5`; the rival's approach lane amber 2.5px when a contact is pending. Tick cost at midpoint, 8px muted.
- System: ground shadow ellipse (owner @0.22, rx 2.2r, ry 0.9r) → 1px stem `20·s` (capital `30·s`) → dot r `4.5·s·1.15` (capital `6·s·1.15`, with halo). Contested: 1px ring. Custodian: dashed ring. Name on **every** node, owner tag on non-own (`NARTH · OKO`). Status label under the ground point: `TAKEN T45` red, `T47 · YOU LOSE` amber.
- Contact spotlight: amber dashed ellipse on the ground at the contested system.
- Fleets in transit: arrowhead on a 14px stem above the lane at progress fraction; label `FLT3 14 · 1 TICK` / `HAL 11 · 1 TICK`.
- **The Fallow** must be the most distinct object: own ground (dithered purple pattern), own light (radial glow 1.6× radius), three stepped rings rising, five raised site pins, label `THE FALLOW · OPENS T60` and race distances `YOU 9 · HAL 7 · SOR 5`.
- Map focus follows the digest: tapping an event's `MAP` action centres and highlights its systems; `MAP · FOCUS: <actor>` in the top-left corner.
