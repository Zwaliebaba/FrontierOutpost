# UI-01 — What the client's screens still cost a player

**Status:** **Items 1.1-1.4, 2.2 and 2.6 done 2026-09-13 (ADR-079 to ADR-083); the rest not started.** Written 2026-09-13 as a UX review and revised the same day against
ADR-077 and ADR-078, which landed between the review and the first item. Item 1.1 is rescoped and
item 2.3 carried a sentence those ADRs made false; both are marked below. Nothing else has been
built. Archive this when the final checklist passes.

Source: UX review of `Design/UI` (README.md, SCREENS.md, DESIGN-GUIDELINES.md, 23 captures) on 2026-09-13.

## How to work this plan

- Work items in order within each phase; phases in order. Each item is one commit (or one ADR + one commit where an ADR is named).
- Before touching a file, read it. Code paths below were taken from `SCREENS.md` and `DESIGN-GUIDELINES.md` and may have moved — grep for the symbol, not the line.
- Every item has **Done when** — do not mark it done without satisfying each bullet. Where it says "capture", retake the named PNG in `Design/UI/screens/` with `Build/Screenshot.ps1` per `README.md` §Photographing the build, and run the luminance check described there.
- Every item that changes what a screen draws must update `Design/UI/SCREENS.md` and, if a token or component rule changes, `Design/UI/DESIGN-GUIDELINES.md`. The docs are the design record; the tree and the docs must agree.
- Existing tests: `Tests/LockstepTests/TapTests.cpp`, `DigestViewTests.cpp`, `JoinPageTests.cpp`, `FaceRuleTests`. Run all of them after every item. Add tests where an item names one.
- Constraints that do not change: 1280×720 canvas; inline pixel layout via `DesignTokens.h` (`Ink`, `Frame`); IBM Plex Mono for data, Plex Sans for sentences; the client never computes combat outcomes (ADR-063); the client writes no files (R13); rail rows link and never order (ADR-060 — **opening a picker is not ordering**, which is why a FLEETS row may open one, ADR-077).
- If an item conflicts with an ADR, write a new ADR amending it rather than silently violating it. ADRs live in `Design/ADR/` (find the numbered series; the last known is ADR-078).
- Items marked **[ASK]** need an owner decision first. Open the question in the ADR, do not guess.

---

## Phase 1 — Costs a player every match

### 1.1 Fleets standing on the map
**Done 2026-09-13 — ADR-079.** Every bullet below is built except the `01-main-page.png` retake, which needs a
board the viewer has played and so needs an unlocked desktop; ADR-079 records what was photographed
instead and keeps the retake as an open question.

**Rescoped 2026-09-13, after ADR-077.** As written this item bundled two problems: fleets being
invisible on the map, and the destination picker having no reliable entry point after tick 0.
**ADR-077 fixed the second** — a FLEETS row on the locks rail opens the picker on every tick, a
fleet already on a lane is refused one everywhere, and `OrdersOf` stopped re-sending an order for a
fleet in flight. That ADR considered map badges as the way to fix reachability and rejected them on
cost, not on merit; what is left here is the problem badges are actually the right answer to, and it
is untouched.

**Problem.** `MapRender.cpp` draws a fleet only while it moves, so a board where the player holds
ten fleets draws none of them: `01-main-page.png` lists ten `HOLD` rows in the rail and carries not
one mark on the map. Where your strength is, and how strong a garrison you are flying into, can only
be read by scanning a column of ten near-identical rows — and since ADR-077 every one of those rows
is a live picker, so the rail is now carrying both jobs alone.

**Change.**
- `MapRender.cpp`: for every system where the player holds ≥1 fleet, draw a badge beside the node disc: a small rounded rect in `Ink::YOU` (94,196,255) fill with ink text = total ships (`10`). Position: right of the disc, baseline-aligned with the name label. Depth-sort with the system. Rival fleets: same badge in the owner colour, muted fill (owner @0.35), so a defended system is legible without revealing more than the snapshot already carries.
- `MainPage.cpp` hit-testing: a tap on your own badge opens the destination sheet for that fleet. Where a system holds several of your fleets — the common case, and `01-main-page.png` has three of them — open a sheet listing them first (`FLT 3 · 3 SHIPS`, `FLT 8 · 4 SHIPS`), then the destination sheet. Tap on a rival badge = focus only.
- **Both routes go through `Action::OpenFleet`**, so ADR-077's guard applies to a badge exactly as it applies to a rail row: a fleet the server has on a lane is never offered a picker. A badge is drawn for parked fleets only, so no badge can name one — the fleet-list sheet is where the check earns its keep, because a system's fleets can depart between the snapshot that drew the badge and the tap.
- Legend: add `● n  FLEET HOLDING` while any badge is drawn.

**Dropped from this item (2026-09-13).** *"Build sheet for a held system carrying your fleet: append
a row `Move FLT n`."* With the rail row and a badge, that is a third route to one picker on one
screen, and it puts a control that is not a build on a sheet ADR-058 keeps about one system's
buildings — the bullet's own caveat that the row "must not count as a build target for ADR-053
guards" is the tell. If badges turn out not to be discoverable enough, reopen it as its own item.

**Done when.**
- `01-main-page.png` shows a badge on Halvorsen, Hollis, Ulme, Brannoc, Nyx.
- Tap test: badge tap opens `MOVE FLT n - PICK LANE`; a system holding several opens the fleet list first; rival badge tap only changes `MAP - FOCUS`; no tap on any badge reaches a fleet under way.
- At the lock, badges are focus-only (same as rail rows).
- SCREENS.md §01 Map and DESIGN-GUIDELINES.md §Map updated; README.md item 6 amended, and both README notes that say a parked fleet has no marker (§Photographing, §the capture list) rewritten — ADR-077 already changed what follows that clause, so check the current text rather than the text quoted here.
- New ADR: "Fleets standing on the map" (amends ADR-055, cites ADR-077 for the guard it reuses).

### 1.2 Digest scrolls; page band says what is hidden
**Done 2026-09-13 — ADR-080.** Built except the `01-main-page.png` retake, which needs an unlocked
desktop. The ranking bullet turned out to be already true — `ConsequenceRank` sorts a loss and a
contact ahead of a rival grouped for offers — so it is pinned by a test rather than changed.

**Problem.** 35 events, 8 visible, 27 behind `1 / 4 · MORE ›`. The player cannot know whether a battle is on page 3. ADR-052 option C forbids scrolling; ADR-061 introduced paging.

**Change.**
- `MainPage.cpp` / `DrawDigestRail`: accept wheel notches and drag over the digest column from `PointerInput` (they are already banked and unread) and scroll the card stack by whole cards (snap to card top). Keep the page band as the touch/keyboard fallback; `PgUp`/`PgDn` page.
- Page band text (`DigestView.cpp`): replace `1 / 4 · MORE ›` with `27 MORE ·` + the worst hidden kind by `ConsequenceRank` when one is hidden: `27 MORE · 1 BATTLE ›`. `‹ PREV` unchanged.
- Ranking: any card carrying a verdict, a system loss or a contact must sort onto page 1 ahead of actor grouping. Check `DigestView.cpp` ranking; the leading card still stays first (ADR-056).
- Scroll position resets when a new digest arrives (same as the page today).

**Done when.**
- `DigestOverflowTapTests` extended: wheel over the column advances by one card; band text includes the hidden count; a hidden battle names itself.
- `01-main-page.png` retaken with the new band.
- New ADR amending ADR-052 option C for the digest column only (sheets and rail stay unscrolled).

### 1.3 Actor cards name their targets; drop redundant `MAP`
**Done 2026-09-13 — ADR-081.** Built except the `01-main-page.png` retake. Plain cards get named
chips too rather than only losing the redundant button: one rule for every card beats two that have
to agree.

**Problem.** A collapsed actor card is `P4 · 5 EVENTS` and four `MAP` buttons. Nothing says what P4 did or where. Plain cards also carry `MAP` when tapping the card already focuses the same system.

**Change.** In `Lockstep/SnapshotView.cpp` (action composition, ADR-057) and `DigestView.cpp`:
- Collapsed actor card: replace the per-event `MAP` buttons with one outlined chip per distinct system, labelled with the system name (`ULME`, `HOLLIS`, `NYX`), max 4, then `+n`. Chip tap = focus that system. Keep the verdict box and any real controls (`ACCEPT`, `REDIRECT FLT n`, a priced build) on the row.
- Plain event card: omit the `MAP` action when its target equals the system the card itself focuses. Keep `MAP` only when the action names a different system than the card tap would (e.g. a production card with a build at another system).
- Open actor card: per-event lines unchanged; the system chips replace the `MAP` buttons here too.

**Done when.**
- `DigestViewTests`: an actor card with events at three systems yields three named chips and zero `MAP`; a plain `CLAIMED HOLLIS` card yields no `MAP`.
- `01-main-page.png` retaken.
- DESIGN-GUIDELINES.md §Components "Actor card" and §Copy "Every event carries its own actions" updated.

### 1.4 Red means your loss only; captured labels age out
**Done in part 2026-09-13 — ADR-082.** The age-out and "your own capture is not red" are built. The
**[ASK]** is still open and is the rest of it: the snapshot carries `capturedAt` and no previous
owner, so a rival taking a system from another rival still reads red. ADR-082's open question puts
the two answers to the owner.

**Problem.** `CAPTURED Tn` is drawn red under every captured system, including the ones you captured. Six red labels on a winning board read as six losses.

**Change.** `MapRender.cpp`:
- Red (`Ink::LOSS`) only when the capture took the system from the viewing player (previous owner == you). Otherwise draw the label in the new owner's colour at 0.7 α when you were not involved, and in `Ink::YOU` when you took it.
- Drop the label once `currentTick - capturedTick > 3`. The snapshot carries the tick; if previous owner is not on the wire, **[ASK]** whether to add it to `SnapshotSystem` or to fall back to "red only when current owner ≠ you and the system was in your last-drawn snapshot as yours" (client-side memory within one session is allowed; nothing is written).

**Done when.**
- `01-main-page.png`: Hollis/Nyx/Brannoc labels blue or gone; only genuinely lost systems red.
- DESIGN-GUIDELINES.md §Palette semantic row and §Map updated.

### 1.5 Connection lost as a banner, not a modal
**Problem.** `ConnectionDialog::Kind::Lost` swallows every tap while its own copy says nothing you tap is sent anyway. The player cannot read the digest or map while dropped. Copy defects: `back 4 time(s) already`; `The tick still locks in 00:00:00` shown after the lock.

**Change.** `ConnectionDialog.cpp`, `MainPage.cpp`, match loop in `Lockstep/Lockstep.cpp`:
- New presentation for `Lost` only: a 44px amber-bordered band directly under the top bar spanning all three columns: `CONNECTION LOST · RECONNECTING IN 2S` left, `RETRY NOW` (filled) and `QUIT` (outlined) right. No scrim.
- While up: every order-giving control is dim and inert (reuse the at-lock inert path from ADR-065); card taps, rail row links, map drag, focus and sheets still work. Sheets show a `OFFLINE` chip where `LOCKED` goes.
- Copy: pluralise properly (`back once already` / `back 4 times already`); when the countdown has passed zero read `T10 locked while you were away.`; before that keep `The tick still locks in hh:mm:ss whether or not you are back.`
- Other dialog kinds (`Connecting` on the join screen, refusals, `WaitingForHost`, `MatchFinished`) stay modal.

**Done when.**
- `ConnectionDialogTapTests`: with Lost up, a rail row tap changes focus; a build button tap sends nothing; `RETRY NOW` triggers a connect; after reconnect the band is gone.
- `04-connection-lost.png` retaken.
- New ADR amending ADR-038/ADR-043.

---

## Phase 2 — Legibility and hierarchy

### 2.1 Second type size
**Problem.** Every string is 12px; hierarchy is weight (one step), case and colour only. The 2× pixel-doubled countdown reads as an artefact beside anti-aliased Plex.

**Change.**
- `Build/BakeFont.py`: add IBM Plex Mono Medium at 16px, hinted, as a third face in `NeuronClient/Font.h`. Keep the four 12px cuts.
- `FontRenderer`: expose a size selector; remove `COUNTDOWN_SCALE` and draw the top-bar countdown and the `LOCKSTEP` title on join/seats in the 16px cut.
- Use the 16px cut for: event-card and actor-card titles, sheet titles (`BUILD - DOTHAN`), dialog titles, the digest header left string. Nothing else.
- Re-derive line boxes that change: card title band, sheet header (36px stays), dialog title row. Everything through `BandTopForText` / `CenterTextY`, no hand offsets.
- Measure ink per DESIGN-GUIDELINES.md §Font and record it there. Delete the "2× only for…" bullets.

**Done when.**
- All 23 captures retaken (the face changed on every screen); luminance check passes.
- `FaceRuleTests` still pass (the discriminator is case, not size).
- ADR-074's open question about the countdown is answered in a new ADR.

### 2.2 Contrast floor
**Done 2026-09-13 — ADR-083.** The estimates below were wrong and the ADR records the measurement:
only `NEUTRAL_DIM` was actually under 4.5:1, at 3.61. All three were raised to the values below
anyway, and `ContrastTests` is the floor from now on. The inert-button bullet was already true in
the tree.

**Problem.** `TEXT_MUTED` (0.55 α) ≈ 4.0:1 and `NEUTRAL_DIM` (0.45 α) ≈ 3.1:1 over `APP_BACKGROUND`. Section headers, the legend, `- nothing queued -`, lane costs and the whole at-lock state fall under 4.5:1.

**Change.** `LockstepClient/DesignTokens.h` only, then the private palette copies in `SeatsPage`, `JoinPage`, `ConnectionDialog` (ADR-045's open item — fold them onto the header now):
- `TEXT_MUTED` 140 → 168 (0.66).
- `NEUTRAL_DIM` 115 → 140 (0.55).
- `TEXT_DETAIL` 0.60 → 0.66 (same byte as muted; keep the two names).
- Inert/at-lock buttons: keep text at `NEUTRAL_DIM`, and draw the border in `Ink::OUTLINE` so the inert state is carried by the frame, not only by alpha.

**Done when.**
- A script (add `Build/Contrast.ps1` or a unit test over `DesignTokens.h`) asserts every text token composited over `APP_BACKGROUND` and over the dialog card colour is ≥ 4.5:1; `NEUTRAL_DIM` ≥ 4.5:1 too.
- Private palette copies removed; three files include `DesignTokens.h`.
- `06-at-lock.png`, `06-at-lock-sheet.png`, `01-finished.png` retaken.

### 2.3 Fleet rows grouped by system
**Problem.** `FLT 13 10 HOLD HOLLIS` — fleet id and ship count are two bare numbers at equal weight; ten rows say `HOLD`.

**Change.** `DrawLocksRail`, FLEETS section:
- Group rows under a sub-band per system: `ULME · 7 SHIPS` (muted), rows `FLT 3 · 3` with the id muted and the count primary. Right column only when the fleet is not holding (`→ PELL` / `T9`, `+DEF` in blue on the incumbent). Fleets under way group under `UNDER WAY`.
- Section header count unchanged (`FLEETS 10`).
- **Row link behaviour unchanged — as ADR-077 left it, not as this line first read it.** A holding
  row opens the destination picker; a row for a fleet already on a lane focuses where it is going;
  at the lock every row focuses. **Regrouping must not change which of those a row does**, and
  `FleetMoveTapTests` is what says so: it sweeps for the control rather than for a coordinate, so it
  survives the new geometry and fails if the behaviour moves.

**Done when.**
- Rail tap tests still pass with the new row geometry (scan for dividers, do not hardcode y — per README).
- `01-main-page.png`, `08-missed-digests.png` retaken.
- DESIGN-GUIDELINES.md "Locks list row" updated.

### 2.4 One filled primary per page
**Problem.** Three filled-blue build buttons on one digest. Filled stops meaning "do this".

**Change.** `SnapshotView.cpp` / `DigestView.cpp`: only the leading card's primary is filled. Every other card's actions are outlined (price still on the label). Queued (outlined blue) and unaffordable (dim) states unchanged. **[ASK]** alternative: filled = affordable now; if chosen, add `AFFORDABLE` to the legend. Default to the first.

**Done when.** `DigestViewTests`: a digest with three priced builds has exactly one `EventAction::primary` filled. `01-main-page.png` retaken.

### 2.5 Map zoom, reset, fleet marker, label collision
**Problem.** Wheel and pinch are banked by `PointerInput` and read by nothing; `MainPage::ResetView` has no control. Fleet and system markers are the same blue and differ only by size. `FLT 1 · ETA T2` overprints the Xander lane.

**Change.**
- `MainPage.cpp`: read banked wheel notches / pinch delta → `OrbitCamera` distance, clamped [0.6×, 2.5×] of the authored framing. Drag stays orbit.
- Top-left of the map pane: after `MAP - FOCUS: PELL` draw an outlined chip `RESET` that calls `ResetView`; drawn only when the camera differs from the authored framing.
- `MapRender.cpp`: fleet in transit becomes a filled triangle (the existing arrowhead) with no disc; systems keep the disc. Update legend `FLEET UNDER WAY` glyph accordingly.
- Label placement: after projecting all labels, run one pass pushing any label that overlaps a lane segment or another label 12px away from the lane normal, prefer up. Cheap greedy pass is fine; determinism required (same frame, same layout).

**Done when.**
- Tap/pointer test: 3 wheel notches change camera distance; `RESET` restores; `RESET` absent at authored framing.
- `01-fleet-under-way.png`, `01-build-sheet.png` retaken; no label crosses a lane in either.
- README item 6 zoom sentence removed; DESIGN-GUIDELINES.md §Map updated.

### 2.6 Legend hidden under sheets
**Done 2026-09-13 — ADR-082.** The first option, one branch, as the item asked.

**Problem.** Every sheet capture shows `YOU  PROPOSED LANE  TRADE LANE` half-clipped under the `CANCEL` bar.

**Change.** `MapRender.cpp` legend: skip drawing when `MainPage` reports a sheet open; or extend the sheet to the pane's bottom edge (drop the 12px lower margin). Pick the first; it is one branch.

**Done when.** All six sheet captures retaken; no legend pixels below the sheet.

---

## Phase 3 — Per screen

### 3.1 Top bar
- Remove `M<id>` until the snapshot carries a match id (`SnapshotView.cpp`); the census becomes `D3/21 · 6 PLAYERS · 31 SYSTEMS`. Keep the clause-dropping order.
- Put `▶ REPLAY Tn` behind a compile-time or command-line flag (`--dev`) until 07 is wired. Remove `NOT YET WIRED` from the stub title when the flag is on; the flag is the disclosure.
- `3RD / 6` chip: border and text in `Ink::YOU` when you lead, `Ink::WARN` when you have dropped a place since the last digest, outline otherwise. The previous place is in the prior snapshot this client drew (session memory only).
- **Done when:** `01-main-page.png` retaken; SCREENS.md §01 top bar updated.

### 3.2 Join screen (`JoinPage.cpp`)
- Token shown by default; `SHOW` becomes `HIDE`. Rationale in SCREENS.md: a token names a seat, not a person, and is read aloud between friends.
- Remove the dashed `SEAT · NOT YET CONFIRMED` box (never populated) and the `JoinPage::SetMatchSummary` footer path (no caller). If a waiting Welcome screen is ever built, they return with it.
- Footer `ALSO: --join SERVER TOKEN` → remove from the player screen; document the flag in `Design/GETTING-STARTED.md` instead.
- `LAST USED` label → remove (nothing is remembered, R13).
- Card height recomputed (`Frame` join constants); `JoinPageTests` targets re-measured.
- **Done when:** `03-join.png`, `05-connecting.png`, `05-refused-*.png` retaken; README tap targets for JOIN re-measured and updated.

### 3.3 Seats screen (`SeatsPage.cpp`)
- `FILL WAITING WITH BOTS`: add a muted one-liner under the footer left when hovered/selected: `Sets every WAITING FOR PLAYER seat to BOT.`
- Disabled `ENTER MATCH ›`: keep it outlined at `TEXT_MUTED` (not 0.45 α) and rely on the footer sentence for the reason. No other change.
- **Done when:** `09-seats.png` retaken.

### 3.4 MATCH FINISHED standings (`ConnectionDialog.cpp`, kind MatchFinished)
- Replace the single line `6 OF 6 · SCORE 35 · LEADER P6 95` with a six-row list: place, `YOU` / `Pn`, score; your row in `Ink::YOU`. Dialog grows to fit (520px wide stays; height from rows). Data: the snapshot's per-player scores — verify they are on the wire (`SnapshotView.cpp`); if only leader + you are present, **[ASK]** to add the standings to `MatchHeader` on finish.
- **Done when:** `05-match-finished.png` retaken.

### 3.5 Destination sheet
- Sort rows by ETA ascending, then name.
- A candidate held by a rival: draw the ship count and `+DEF` in the owner's colour rather than body colour. No verdict (ADR-063 unchanged).
- **Done when:** `01-destination-sheet.png` retaken; SCREENS.md §01 Destination updated.

### 3.6 Signal sheet
- Pin the `CONCEDE` band and `Concede` row above the `CANCEL` bar and exclude them from the six-row cap so real signals never lose a slot to it. Two-tap and red-from-first-tap behaviour unchanged (ADR-064).
- **Done when:** `01-signal-sheet.png`, `01-signal-sheet-armed.png` retaken; ADR-064 amended.

### 3.7 Missed digests
- When the backlog window was clipped at eight ticks, add a muted 17px line under the delta box: `Older ticks were not kept.` (Detect: `unreadTicks > 8` in the match loop.)
- **Done when:** unit test in `DigestViewTests`; ADR-044's open question closed.

### 3.8 Input target decision **[ASK]**
- Copy says "tap" everywhere; buttons are 18px, rail rows 21px. Decide mouse-only or touch. If touch: buttons ≥ 32px in the digest and rail rows ≥ 32px, which changes every layout constant — do it as its own plan. If mouse-only: change "tap" → "click" in every player-facing string and in DESIGN-GUIDELINES.md, and state the decision under §Frame.

### 3.9 Case **[ASK]**
- Propose mixed-case event-card titles (`Battle at Ulme`) with uppercase reserved for chips, section headers, and status words (`LOCKED`, `CAPTURED`). Blocked on `FaceRuleTests` discriminator (it tells label from sentence by lowercase presence) — replace with an explicit face tag on each string. Write the ADR and stop; do not implement without the owner's answer.

---

## Order of execution

1. 1.1 Fleet badges + MOVE from sheet
2. 1.2 Digest scroll + page band
3. 1.3 Actor-card targets
4. 1.4 Red only for losses · 2.6 Legend under sheets
5. 2.2 Contrast floor · 2.1 Second type size (2.1 forces a full recapture — do 2.2 first so one recapture covers both)
6. 1.5 Connection-lost banner
7. 2.3, 2.4, 2.5
8. 3.1 – 3.7
9. 3.8, 3.9 — ADRs only, await owner

## Final checklist (after the last item)
- Every capture in `Design/UI/screens/` is from the current tree; luminance check passes on all 23 (+ any new ones).
- `README.md` §Screens table statuses and "What the design asks for that the tree does not have" list updated — remove items 6 (zoom, fleet mark) as far as built.
- `SCREENS.md` and `DESIGN-GUIDELINES.md` describe the tree, present tense only for what the tree does.
- All tests pass; the tap-target coordinates in `README.md` §Photographing are re-measured.
