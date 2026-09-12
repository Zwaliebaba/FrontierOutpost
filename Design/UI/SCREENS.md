# Screens

## 01 · Main page (`01-main-page-5a.png`)
**Top bar (44px):** `LOCKSTEP` · `M0419 · D12/21 · 12 PLAYERS · 61 SYSTEMS` · spacer · `T47 LOCKS` + 16px amber countdown · `SCORE 1284` + chip `4/12` + `LDR HALVORSEN 1610` · `REPLAY T46` · `SHARE TICK`.

**Digest (400px, left) — the order surface.**
Header `SINCE YOU LOOKED · T43 > T46` + chip `3 TICKS`; four-cell delta box. Then events in consequence order, each with its actions:
1. red `PELL LOST TO SORNE · T45` — MAP · REBUILD LANE > DUNMORE
2. amber actor card `HALVORSEN · LEADER 1,610 · 3 EVENTS` — outpost at Kepler-Reach / 11 ships arrive T47 / proposes lane +6/tick 3 ticks — verdict box `FLT3 ARRIVES T47 · YOU LOSE` — REDIRECT FLT3 (filled) · ACCEPT LANE · DECLINE · MAP
3. neutral `OKONKWO GONE · CUSTODIAN T43` — MAP
4. purple `THE FALLOW OPENS T60 · 14 TICKS` — PLAN ROUTE
5. blue `INCOME +38 · RESEARCH +4` — BUILD SHIPYARD -20 (filled) · MINING TAMSIN -15
6. red `SORNE SILENT 4 TICKS` — WITHDRAW · HOLD FIRE >
Rows: 6px vertical padding; must fit 720 with all six visible at reference data. Overflow scrolls.

**Map (centre):** per guidelines; focus = Halvorsen; legend YOU / LANE / PROPOSED / FALLOW, and `FLEET UNDER WAY` while anything is in transit.

**A fleet's route (ADR-055).** Every fleet under way draws a line of travelling dots from its origin to its destination, in its owner's colour, on the plane with the lanes. The dots walk toward the destination; the marker sits at the fraction of the lane the fleet's remaining ticks put it at. It is the only thing on the map that moves on its own.

**Locks (260px, right) — read-only summary of what goes in.**
`LOCKS T47` / `UNLOCKED` (amber). One line of help. Sections FLEETS · BUILDS · SIGNALS · PROPOSALS, each row label + status. Footer `ALL LOCK TOGETHER · 02:14:09`. Tapping a row jumps to the event that owns it; no controls live here.

**One order per event (ADR-057).** A build button is offered on the event that would make you want it and never on two cards at once: a claimed system offers a building at *that* system, a production line offers whatever is still unoffered. `MAP` focuses the system the event is about.

**Always one control (ADR-056).** The top bar's `LDR` field is drawn only when the leader is somebody else; while you lead, the `1ST / 6` chip and the score beside it already say it. A digest card that offers nothing to act on gets the standing moves - a priced BUILD and up to two `MOVE <fleet>` buttons for fleets that are not already under way - so a quiet tick is never a screen with no way to give an order.

**The price is on the button (ADR-053).** The top bar carries the purse (`26 CR`) beside the score. The BUILDS header reads `2 AVAIL - 26 CR`, a queued row reads `QUEUED -20`, and a line under the queue says what is left at the lock. Every build control — the digest's filled button (`SHIPYARD JANDAL 20 CR`), the build sheet's rows (`20 CR`) — carries its price; a queued one says `QUEUED` and is outlined rather than filled; one the purse cannot cover says `NEED 7 MORE`, is drawn dim, and is not a target. A refusal in the digest names the building, the system, the price and the purse (`Shipyard at Jandal - costs 20, you had 13`) and focuses the system.

**Sheets (ADR-052).** The four panels — build, destination, signal, replay — are one component, drawn as a sheet against the bottom of the map pane. The destination picker is the one that uses every field: owner square, system name, `YOURS`/`UNCLAIMED`/`<RIVAL>` with `- CAPITAL` / `- CONTESTED` appended, and a right-hand `N TICKS - ETA Tk` in which the lane cost and the arrival tick are two separate facts. Build and signal rows carry a status on the right (`QUEUED`, `SENDING`, `TAP AGAIN TO CONFIRM`) instead of folding it into the title. Replay is a six-phase list and still a stub.

**Behaviour:** countdown live; at zero → screen 06 state. Actions edit local orders until lock. Accept/Decline is an order. Event `MAP` focuses the map. Actor card expands/collapses.
**Code:** replaces the three-rail layout in `MainPage.cpp`. Digest from `TickLog` + previous unread ticks; grouping and ranking are client-side presentation over the same events. Verdict from the combat resolver preview.

## 02 · Share tick (`02-share-tick-5b.png`, 480×640)
Exported PNG from `SHARE TICK`. 2px border in the top event's colour. Top 340px: map crop centred on the top event's systems (same renderer, `slice`). Below: `LOCKSTEP · M0419 · T46 > T47` muted; 16px headline in first person (`HALVORSEN MEETS ME AT KEPLER-REACH.`); one amber sentence; the four-cell delta; `4TH OF 12 · 1,284 · LEADER HALVORSEN 1,610`. Written to disk / clipboard; no network.

## 03 · Join (`03-join-3a.png`)
Centre column 480px over the star field. `LOCKSTEP` 16px + `JOIN A MATCH · ONE SEAT PER TOKEN`. Card: SERVER field (last used) · TOKEN field (masked, SHOW) · one line explaining a token names a seat · seat preview `4 OF 12 · YOU ARE BLUE` (from the server's Welcome once the token validates, dashed box until then) · `JOIN >` filled. Footer: match summary · `ALSO: --join SERVER TOKEN`.
**Code:** first screen when no `--join` args. Needs text input (ADR-014 has none): implement a minimal field or keep CLI and show this screen as confirmation only. Sends `Hello{token}`; on `Welcome` → 01 (via the Welcome dialog if unread ticks > 0); on `Refused` → 05.

## 04 · Connection lost (`04-connection-lost-3b.png`)
Scrim over the live 01. Dialog amber: `CONNECTION LOST` · when the server stopped answering · `Reconnecting · attempt n · next in ks` · unlocked orders are kept locally and re-sent · the lock still runs. Actions QUIT · RETRY NOW. Countdown in the dialog keeps ticking.

## 05 · Connection states (`05-connection-states-3b.png`)
One dialog per outcome, same component:
- `CONNECTING` neutral — server, `Sending token · waiting for Welcome`, progress bar, CANCEL.
- `REFUSED · UNKNOWN TOKEN` red — BACK · EDIT TOKEN. (`RefusalReason::UnknownToken`)
- `REFUSED · SEAT IN USE` red — BACK · RETRY. (`AlreadyConnected`)
- `MATCH FINISHED` neutral — final standings line · QUIT · VIEW LAST DIGEST. (`IsFinished()`)
- `WELCOME · SEAT n OF 12` blue — match, tick, lock time, `k digests waiting since you last looked` · ENTER >.
Dedicated server assumed: no host-left state.

## 06 · At lock (`06-orders-locked-3c.png`)
Applies to 01 at countdown zero: top bar `T47 LOCKED · 00:00:00`; locks rail header flips to filled grey `LOCKED`; a one-line notice `Resolving T47. Controls return with the new digest. Anything you tap now is an order for T48.`; every action in the digest and every locks row goes to 40% opacity and inert; digest header right side `T47 PENDING`. Ends when the new `TickLog` arrives → digest re-renders with `SINCE YOU LOOKED`.

## 07 · Replay (`07-replay-3d.png`)
600px panel centred over the map. Header `REPLAY T46 · PHASE 3 OF 6 · MOVEMENT` · `[X] CLOSE`. Left 190px: six phases (LOCK · PRODUCTION · MOVEMENT · COMBAT · CLAIMS · DIGEST) — done = primary + `OK`, current = filled blue, upcoming = dim. Right: one muted rule-of-thumb line, then one line per `PhaseRecord` entry, then a note that the map shows the board as of this phase. Footer keys `[<] [>] STEP · [D] JUMP TO DIGEST` + PREV / NEXT. Map behind renders the snapshot for the current phase.
**Code:** reads `TickLog.phases` in order; replaces the phase-list stub.

## 08 · Missed digests (`08-missed-digests-3e.png`)
Applies to 01 when unread ticks ≥ 2: header `DIGEST · 3 TICKS WAITING · 16 EVENTS`; tabs `T44 · 5 | T45 · 4 | T46 · 7` (latest active); the amber delta box; latest tick's events expanded; older ticks collapsed at the bottom as `T45 · 4 EVENTS · UNREAD — <top event> — OPEN >`. Reading a tick marks it read.
**Code:** requires the server to retain more than the latest digest (ADR-028 open question). Until then the tabs are one deep.
