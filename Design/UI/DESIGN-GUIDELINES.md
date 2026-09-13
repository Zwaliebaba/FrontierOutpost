# Design guidelines — LockStep: Universe client

**As built, 2026-09-12.** Every number here is one the code draws; where the 2026-09-11 handoff
asked for something else, the handoff's value is given beside it as the intent. The tokens
themselves live in `LockstepClient/DesignTokens.h` (`Ink`, `Frame`), which is the one list that
ships — this document cites it and does not restate what it does not have to.

## Frame
- 1280×720 logical pixels, fixed and presented 1:1 (ADR-011). No scaling factor, no fullscreen.
- Main page: rows `44 | fill`; columns `400 | fill | 260` (digest · map · locks) —
  `Frame::TOP_BAR_HEIGHT`, `DIGEST_WIDTH`, `ORDERS_WIDTH`. The map is what is left between the rails
  and is drawn first; the rails' opaque backgrounds are what confine it (ADR-017).
- 1px separators `rgba(255,255,255,0.10)` (`Ink::CARD_BORDER`, 26/255).
- Rail padding 14px. Card padding **10px** (`MainPage::CARD_PADDING`; the handoff said 8). Line
  height **is not a number here** — it is `FontRenderer::LineHeightPixels`, 17px for the face as
  baked; see §Font. Everything on whole pixels.
- Join card: a 480px column centred, the card at y=228, 282 tall; fields 30px high.
- Seats console: 960px centred at y=180, sized to its contents (owner, 2026-09-12); seat cards
  212×120 in a 3×2 grid; a 256px detail panel on the right; a 44px footer.
- Dialog: **520px** wide (the handoff drew 440 for five side by side; one at a time over 1280 can
  afford the room and the CONNECTION LOST paragraph needs it), 18px padding, centred vertically
  with its top never above y=40.
- Sheet (ADR-052): the map pane's width less a 12px margin, anchored to the pane's bottom; 36px
  header, **44px rows**, 40px `CANCEL` bar, a 24px row for the count that did not fit, six rows at
  most. A full sheet is 340px of the 676px pane, so more than half the map stays visible.

## Font

**This section changed on 2026-09-13 and the change is only half landed. Read the state note first.**

Two families, **four** cuts, baked from TTF into `NeuronClient/Font.h` by `py Build/BakeFont.py`
(ADR-073) and drawn anti-aliased (ADR-074):

- **IBM Plex Mono** — Regular, Medium — the *data* face: every rail row, status, number, chip,
  button label, section label, card title, top bar, countdown, sheet row and legend.
- **IBM Plex Sans** — Regular, Medium — the *sentence* face: event-card detail lines, the rail's
  help line, dialog paragraphs, sheet second lines, the join screen's explanatory lines.
- **The rule.** If the text aligns with something or carries a number, it is mono. If it is a
  sentence with a full stop or a question mark, it is sans.

Both are baked at **12px**, **hinted**. Measured from the files on 2026-09-13: Plex Mono is exactly
0.600em, so a mono column is **7px** — one narrower than the 8×8 font it replaced — and cap height
is 0.698em, which keeps capitals within half a pixel of the height they had.

A line box is ascent 13 plus descent 4 = **17px**, and a line is set at **17px** too. The face
reports a line height of 16 — it carries a negative line gap — which is a pixel less than the box
it asks for, so `FontRenderer::LineHeightPixels` floors at the box. Two numbers that disagree about
how tall a line is, one used by the layout and one by the clipper, is not a disagreement worth
keeping. **Nothing hard-codes a line height any more**: it was 12 in five files until 2026-09-13,
which is what drew a card's detail line through the button under it.

**Hinting is on, and it was checked rather than assumed.** Unhinted at 12px produces *zero* fully
covered pixels in either family — every stem lands across two columns at partial coverage. Crisp
beats faithful on a screen that is mostly columns of small data. It is a parameter of the bake, so
revisiting it is a re-bake and a screenshot, not a code change.

**Coverage is gamma-corrected before it becomes alpha.** The back buffer is `R8G8B8A8_UNORM` and
deliberately not `_SRGB` (ADR-011), so the blender mixes sRGB-encoded values as if they were
linear, and light-on-dark that lands too dark — a half-covered pixel reaches 50% of the string's
brightness where it should reach about 73%. `TextPS.hlsl` raises coverage to 1/2.2, which for a
black background is exactly what blending in linear space would have produced. A fully covered
pixel and an uncovered one are both untouched by it.

**Why four cuts and not ADR-074's five.** Plex Mono SemiBold was dropped on 2026-09-13 under the
ADR's own instruction — *"if at the final size two of them are indistinguishable, the answer is to
drop a cut rather than to keep a difference nobody can see."* At 12px, as post-gamma coverage over
`SHIPYARD L1 HOLLIS 20 CR`: Regular → Medium is **+15.4%** ink, Medium → SemiBold **+9.1%**; solid
pixels go 10%, 18%, 24%. The first step is obvious side by side and the second is not. The sans
pair was measured the same way and **kept** — Regular → Medium is +23.1%, and 4% solid against 15%.
The structural argument is the stronger one: SemiBold was set in exactly one thing, the lock
countdown, which is already the only amber on the bar and already twice the size. **ADR-074's
Decision still says five; amending it is the owner's call.**

**State, 2026-09-13.** Stages 0 to 6 of
[`Design/Plans/FONT-01-PlexFaces.md`](../Plans/FONT-01-PlexFaces.md) are built. Stage 7 — restoring
the five characters ADR-014 substituted and re-measuring the six strings it shortened — is not.

- 2× (`FontRenderer::COUNTDOWN_SCALE`) **only** for the lock countdown on the top bar and the
  `LOCKSTEP` title on the join and seats screens — the one thing read first on each. It is a whole
  pixel-block enlargement of the 12px face, which is still the wrong tool: ADR-074 leaves open
  whether the countdown should become a larger baked cut instead, and that is now the *only* way
  left to make it heavier, since the weight above it was dropped. The handoff's other 2× use, the
  share-card headline, has no screen to be on.
- Emphasis is colour, case and now **weight** — one step of it, Regular against Medium. Labels and
  headers are uppercase (`Uppercased()`), sentences mixed case. **The shouting is no longer forced
  by the font**: the 8×8 face had no lowercase and Plex has both, so it is a choice the sheet is
  making, and ADR-074 left open whether it should go on being made. One thing depends on the
  current answer — `FaceRuleTests` tells a label from a sentence by whether it carries a lowercase
  letter — so changing it means giving that test a different discriminator. No italics, no
  letter-spacing.
- **Chrome that sits around already-placed text goes through `BandTopForText`**, the inverse of
  `CenterTextY`. Three places had their own hand-tuned offset — a chip beside a section header, the
  buttons under a card's last line — each of them the number that centred an 8px glyph, and each
  wrong in the same direction once a box was 17px. A button is still **18px**: the box grew, the ink
  in a shouted label did not.
- Detail text wraps by word to a PIXEL WIDTH (`FontRenderer::WrapToWidth`). It wrapped to a
  character count until 2026-09-13; there is no character count any more, because there is no one
  advance for a proportional face to have.
- **"31 characters a line" is retired.** The digest rail is 254px and always was; what fits in it is
  the font's to answer, and at the 7px mono column that is **36**. Every such number in this
  directory is a fact about the face and moves when the face does.
- **The middle dot is back.** `·`, `−`, `–`, `→` and `›` are all baked, so the five substitutions
  ADR-014 was forced into can be undone. **They have not been yet** — that is FONT-01 stage 7 — so
  the build still reads `SINCE YOU LOOKED - T43 > T46` and `M0419 - D12/21`.

## Palette (8-bit RGBA)
The built values are `Ink` in `DesignTokens.h`; the fractions below are the handoff's, with the
byte the build uses where it differs by more than rounding.

UI
- `rgb(11,14,20)` background / ink (`APP_BACKGROUND`); a dialog's card is `rgb(17,21,29)`.
- `rgba(255,255,255,0.10)` structural line (`CARD_BORDER`) · `0.07` row divider (`DIVIDER`) ·
  **`0.20`** outlined-button border (`OUTLINE`, 51/255; the handoff said 0.25) · `0.04` card fill
  (`CARD_FILL`) · `0.08` hover (`HOVER_FILL`, drawn on the locks rail row under the pointer and
  nowhere else, ADR-060).
- `rgb(240,243,247)` primary text · body/detail `rgba(214,220,228,0.60)` (`TEXT_DETAIL`) ·
  muted **`0.55`** (`TEXT_MUTED`, 140/255) · dim `0.45` (`NEUTRAL_DIM`).
- `rgba(214,220,228,0.59)` the filled grey a locked rail wears (`LOCKED_FILL`, SCREENS.md 06).
- Scrim under a dialog `rgba(7,9,13,0.80)` (the handoff: `rgba(6,8,12,0.74)`).
- Map ground: vertical gradient `rgb(8,10,16) → rgb(16,22,36) @45% → rgb(11,14,20)`; grid
  `rgba(94,196,255,0.07)`; the horizon glow is declared and not drawn. Stars: a band and a
  brightness range (ADR-033), brightest `rgba(214,220,228,0.86)`.

Semantic / owner (ADR-027: you are always blue)
- `rgb(94,196,255)` **you** · primary action · trade lane · proposed lane · accept · a queued row.
- `rgb(255,196,87)` warning · countdown · deadline chip · contact events · proposals awaiting you ·
  the verdict box · an armed concede · the practice offer.
- `rgb(255,110,96)` loss · captured · refused · a queued concede · `RECONNECTING`.
- `rgba(214,220,228,0.35)` neutral: custodian, economy and ignored events' dots.
- `rgb(170,140,255)` the sealed region.
- Rivals take their colours from the owner's player index (`OwnerColor`, twelve distinct); a player
  is `YOU` or `P2` until identity exists, and the empire names (`HALVORSEN`…) appear only on the
  seats screen. Names are on every map node, so colour is never the only channel.

`SeatsPage`, `JoinPage` and `ConnectionDialog` still carry private copies of this palette
(ADR-045's open item); the values are the same today and the header is the one to change.

## Components
- **Button, filled** — `you` blue, ink text, 18px high in the digest (24 in a dialog, 22–24 on the
  seats and join screens), 6px side padding. **At most one per card**: the action the digest thinks
  you should take (`EventAction::primary`).
- **Button, outlined** — 1px `OUTLINE`, primary text. A build button carries its price and has two
  more states (ADR-053): queued → outlined blue with ` - QUEUED`, so the next tap is known to take it
  back; beyond the purse → dim, ` - NEED 7 MORE`, and not a target. At the lock every button is dim
  and inert except `MAP`, which still focuses. Buttons that do not fit the column's width are dropped,
  not wrapped. The amber warning variant (`PROPOSE`) is drawn on the rail's trade-lane row, which
  nothing produces yet.
- **Chip** — 1px border, 7px side padding, text in the border colour: `3 TICKS` (amber, digest
  header), `4TH / 12` (outline, top bar; ordinal because `4/12` reads as a fraction). The rail's
  `LOCKED` chip is filled grey rather than outlined.
- **Section header** — a 22px band under a divider: label left, count right, both muted.
- **Event card** — a 1px divider on top, an 8px **dot** (not a square) in the kind's colour, the
  title uppercased in primary, detail lines wrapped in body colour and indented 18px, then the
  verdict box and the action row. The whole card is a target that focuses its system. No stamp on
  the right for a plain event (the handoff's `T45`), and no highlighted variant.
- **Actor card** — an event card whose title is the player (`P3 - LEADER 1,610` when they lead),
  stamp `3 EVENTS`, one line per event, the verdict from whichever of their events carries one, and
  every action of every event in one row. Ranked by its worst event. **Collapsed unless it is the
  open one** (ADR-061): the per-event lines sit behind a tap on the 22px title band, everything else
  is drawn either way, and one card is open at a time.
- **Digest page band** — 22px at the foot of the digest, drawn only when the card stack is taller
  than the column: `1 / 3 - MORE >` right, `< PREV` left once past page one, both muted (ADR-061).
  Page breaks fall between cards; page 1 always carries the leading card and so the standing moves.
- **Verdict box** — 1px amber border inside the card; line 1 amber `FLT1 ARRIVES T47 - YOU LOSE`,
  then the numbers in body colour, always saying whose ships remain.
- **Sheet** — the panel component (ADR-052): ink background, 1px border; 36px header with the title
  left and `X` right, the whole 36×36 corner tappable; rows of an 8px owner square + title +
  optional second line + optional right-aligned status, **44px whatever the content** (one line
  centres, two sit either side of the middle); a full-width `CANCEL` bar; six rows, a seventh
  reported in a muted 24px row rather than dropped. A row with nothing to act on is drawn dim and is
  not a target. A **section band** — 22px, a rule and a muted label, never a target — separates a row
  that is different in kind from the ones above it; it counts against the six and is dropped before a
  real row is (ADR-064). A row said in red is one that cannot be taken back: today the armed or
  queued `Concede`, and nothing else. **At the lock a sheet stays open and goes inert** (ADR-065):
  every row dim, the filled grey `LOCKED` chip in the header clear of the `X`, and the rail's lock
  sentence in amber under the header; `X` and `CANCEL` still close it.
- **Dialog** — the connection component (ADR-038): a 520px card, 1px border in the tone
  (blue welcome / amber lost / red refusal / hairline neutral), the title in the tone's colour —
  primary text for a neutral tone, because the hairline at 26 alpha is unreadable as text — a
  wrapped paragraph or three in body colour, and buttons laid out from the right with the primary
  filled and rightmost. It sits on the scrim over the live page and swallows every tap it is over.
- **Text field** (ADR-034 §3) — label in muted above a 30px ink box, a right-hand word on the
  label line (`LAST USED` is static; `SHOW` is a control), border blue when focused, text inset 10px,
  a `_` caret blinking on a one-second period, masked as `*`. Printable ASCII, Backspace, Delete,
  Left/Right/Home/End, Tab between fields, Enter submits; 48 characters for a server, 32 for a token.
- **Seat card** — swatch, `SEAT 01`, `YOU` or the empire name, `TOKEN` and the token, a status line
  (`CONNECTED - YOU` / `CONNECTED` / `WAITING FOR PLAYER` / `BOT WILL TAKE OVER` / `BOT - STEADY`),
  and the three-way `HUMAN | BOT AT T1 | BOT` — one control for who plays this seat (ADR-066), its
  segments sized to their labels because a third of a 212px card is seven glyphs and `BOT AT T1` is
  nine; blue border when selected.
- **Tabs (unread ticks)** — not built.
- **Locks list row** — label primary left, wrapped to leave room; status right, coloured:
  `T7`/`HOLD` muted, `+DEF`/`QUEUED -20`/`SENDING` blue, `PROPOSE`/`3 TICKS` amber, `CONCEDE` red.
  A row is a **link** to what it names (ADR-060) — the build sheet, the fleet's location or
  destination picker, the far end of a proposed lane — and gives no order; a row with nothing to
  point at is not a target. `HOVER_FILL` under the pointer, on targets only. Focus-only at the lock.

## Copy
- Ops-console terse, numbers first, ` - ` between facts: `PRODUCTION +17`, `CLAIMED PELL`,
  `Shipyard at Jandal - costs 20, you had 13`.
- The digest header is `DIGEST - TICK 46` with `7 EVENTS` on the right — or, when two or more
  ticks resolved while this client had nothing on the screen, `SINCE YOU LOOKED - T43 > T46` with
  a `3 TICKS` chip and the four-cell delta box under it. The delta shows only cells that are
  non-zero (`-1 SYSTEM · 1 CONTACT · 2 PROPOSALS · 1 LANE LOST`); four cells that always read
  `0 CONTACTS` teach a player to stop reading them. At the lock the right-hand figure is
  `T47 PENDING` in amber.
- Consequence order: system lost > contact > proposal > custodian/absence > region timer > income >
  silence (`ConsequenceRank`); stable, so ties keep the server's severity order (ADR-020).
- Group by actor when one rival produces ≥2 events; the card is ranked by its worst; `LEADER 1,610`
  in the title when they lead.
- Under `SINCE YOU LOOKED` only, fold a run of repeats into one card with the total and the window's
  span — `PRODUCTION +18 - T6 > T9` (ADR-062). Never a contact, a capture, a proposal or a verdict.
- Combat preview is a verdict: `FLT1 ARRIVES T47 - YOU LOSE` / `YOU WIN` / `HOLD`, then
  `You arrive 14. P3 holds 11 +def. 6 of theirs remain, 0 of yours.` Never a bare `A v B`. The
  destination sheet says what is standing on a candidate (`P3 - 11 +DEF`) and not how it would go:
  the verdict is the server's arithmetic and the wire carries one only for where a fleet is already
  flying (ADR-063).
- **Every event carries its own actions**, from a fixed vocabulary: `MAP`; `REDIRECT FLT 1` on a
  contact your fleet is flying into; `ACCEPT` / `DECLINE` on a proposal; one priced build
  (`SHIPYARD JANDAL 20 CR`) — on a claimed system for that system, on a production line for whatever
  is still unoffered, and never the same row on two cards (ADR-057). A digest that offers nothing
  to act on gets the standing moves on its leading card — `BUILD 20 CR` and up to two `MOVE FLT 1`
  for fleets not already under way (ADR-056); an empty digest is a card that says so
  (`NOTHING HAS HAPPENED YET` before the first lock, `A QUIET TICK` after) with the same moves.
- Signals are the picker's rows (ADR-039), not free text: `Open lane - Pell to Dothan`,
  `Share scouting - P2`, `Hold fire 3 ticks - P2`, `Withdraw <offer> - P2`, `Close lane - A to B`,
  `Concede` — the last always last, under a `CONCEDE` band and in red from the first tap (ADR-064),
  needing two taps, the row saying `TAP AGAIN TO CONFIRM` between them. The handoff's `REBUILD
  LANE`, `PLAN ROUTE`, `WITHDRAW`, `HOLD FIRE` card actions are these rows.
- The price is on every build control and the purse is on the top bar (`26 CR`) and the BUILDS
  header (`2 AVAIL - 26 CR`); a queued row reads `QUEUED -20` and a line under the queue says what
  is left at the lock (ADR-053).
- The rail's one line of help says what the column is and what its rows do: *What goes in when the
  clock hits zero. Tap a row to go to what it is about.* — or, at the lock, *Resolving T47. Controls
  return with the new digest. Anything you tap now is an order for T48.*
- System count and player count come from the match, never hard-coded. The census on the top bar
  is dropped a clause at a time to fit in front of the countdown, never clipped mid-word.

## Map
A graph on a ground plane authored in an 800×560 design space (`MapView`), seen through a real
orbit camera (`Neuron::OrbitCamera`, ADR-017) that the player drags to orbit — yaw and pitch, the
ground moving with the finger — and that frames the whole galaxy once from the graph's extent
(ADR-034 §1). The sky is a sphere of directions projected through the same camera (ADR-032), so it
turns rather than slides.

**The handoff's projection is provenance, not spec** (ADR-034). It describes how the mockups were
rendered and is kept here for that reason only:

```
d  = y / 560
s  = 0.5 + 0.65·d
sx = 400 + (x − 400)·s
sy = 60 + 440·(0.3·d + 0.7·d²)
```

The consequence: the map pane is never pixel-identical to its mockup; every other pane is fixed.

Drawn, in painter's order (`MapRender.cpp`):
- The gradient ground, then grid lines of constant x and y projected, then the stars.
- Lanes on the plane: plain `rgba(214,220,228,0.28)` 1.2px; a trade lane blue 2.5px; a proposed
  lane blue dashed `4 5` (the handoff: amber). The tick cost at each lane's midpoint, 8px detail.
- **A fleet's route** (ADR-055): travelling dots from origin to destination in the owner's colour
  (2.5px on a 7.5px gap, 18px/s toward the destination), for every fleet under way, under
  everything that stands on the plane. The only thing on the map that moves on its own.
- The sealed region: a dashed purple ground circle of radius 62 with a faint fill, a second ring
  lifted 26 units, three site pins 14 units tall, and `SEALED - OPENS T60` under the near rim.
- Systems and fleets back to front (a depth sort, because painter's order is the whole occlusion
  model). A system: ground shadow ellipse (owner @0.22, 2.2× wide, 0.9× tall) → 1px stem (20 units,
  capital 30) → the dot (radius 4.5·1.15, capital 6·1.15 with a halo at 2.4×). Contested: a 1px ring.
  Custodian: a dashed ring and `CUSTODIAN T43` under the ground point. Captured: `CAPTURED T45` in
  red. Focused: a white ring at 3× the radius. The name above every node, capitals uppercased, 8px at
  every distance.
- A fleet in transit: a 14-unit stem, an arrowhead pointing along the lane in world space, and
  `FLT 1 - ETA T6` — yours above the head, a rival's beside and below, so two converging on one
  system cannot overlap. The marker is clamped a label's half-width (68px) clear of both ends, and a
  lane too short for that draws it in the middle (ADR-059).
- `MAP` in the top-left corner, `MAP - FOCUS: PELL` once the digest or a tap has pointed it at a
  system. The legend along the bottom: `YOU`, up to four rivals actually on the map, `PROPOSED LANE`,
  `TRADE LANE`, and `FLEET UNDER WAY` while anything is in transit.
- Tapping a system you hold opens its build sheet; a system you do not hold only focuses
  (ADR-058); tapping your own fleet's marker opens its destination picker.

Not drawn (the handoff's list, kept as intent): owner tags beside non-own names (`NARTH · OKO`);
the contact spotlight (an amber dashed ellipse at a contested system); a verdict label under a node
(`T47 · YOU LOSE`); the rival's approach lane in amber when a contact is pending; the Fallow as
"the most distinct object" — dithered ground, radial glow at 1.6× radius, three stepped rings, five
pins, `THE FALLOW · OPENS T60` and the race distances `YOU 9 · HAL 7 · SOR 5`; focus by actor
(`FOCUS: HALVORSEN`); zoom (wheel and pinch are banked by `PointerInput` and read by nothing) and a
way back to the authored framing (`ResetView` exists; no control calls it).
