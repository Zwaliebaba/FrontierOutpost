# Screens

Per screen: what the build draws, what the 2026-09-11 handoff asked for that it does not, and where
the code is. Status is as of 2026-09-13, and each heading names its capture in `screens/`; every
capture ADR-069 and ADR-070 touched was retaken that day from the Debug build carrying them, and the
join and connecting screens are 2026-09-12's, which nothing since has moved. The handoff's mockups
are no longer in the tree (`README.md` says where they went). Numbers 01–08 are the handoff's; 09
was added by ADR-036 and sits between 03 and 01 in the flow (`README.md` draws it). "Not built" is
stated as such, and nothing below is in the present tense unless the tree does it.

## 01 · Main page — **built** (`01-main-page.png` is the capture, tick 11 of a six-seat match)

**Top bar (44px, `MainPage::DrawTopBar`).** `LOCKSTEP` · `M0007 · D2/21 · 6 PLAYERS · 26 SYSTEMS`
· spacer · `T8 LOCKS` + the countdown at 2× in amber · `26 CR` · `SCORE 1,284` + chip `4TH / 6` ·
`LDR P3 1,610` · `▶ REPLAY T7`. The left sentence is measured against what the right block leaves
and dropped a clause at a time (`- ENDS <date>` first, then the census, then only the stem). While
the link is down `RECONNECTING` follows it in red. The `LDR` field is drawn only when somebody else
leads (ADR-056). At the lock the countdown turns grey and the label reads `T8 LOCKED`; when the match
is over, `MATCH ENDED --:--:--`.

*Differs:* no `SHARE TICK` button (screen 02 is not built). `M<id>` is the tick number, zero-padded
— the snapshot carries no match id — and `D<n>/21` assumes four ticks a day and a 21-day match
(ADR-051 records the defect); `endsAt` is never filled, so the `ENDS` clause never shows.

**Digest (400px, left) — the order surface (`DrawDigestRail`, `DigestView.cpp`).**
Header `DIGEST - TICK 7` with `5 EVENTS` on the right, or `SINCE YOU LOOKED · T4 → T7` with an
amber `3 TICKS` chip and the delta box when ticks resolved unseen (see 08). Then cards in consequence
order, each with its actions on its own row: an event card (dot, uppercased title, wrapped detail,
verdict box when your fleet is flying into a contact, actions), or an actor card grouping a rival's
≥2 events (`P3 - LEADER 1,610`, `3 EVENTS`, one line per event, ranked by the worst). Actions are
composed per event (ADR-057): `ACCEPT`/`DECLINE` on a proposal, `REDIRECT FLT 1` on a contact your
fleet reaches, a priced build on a claimed system or a production line, `MAP` on anything about a
system. When no card offers a real control the standing moves go on the leading card — a priced
`BUILD` and up to two `MOVE FLT n` (ADR-056); before the first lock the digest is one card,
`NOTHING HAS HAPPENED YET`, carrying them. Build buttons show `- QUEUED` (outlined blue) and
`- NEED 7 MORE` (dim, inert) per ADR-053, and name the LEVEL they would build —
`MINING STATION L2 JANDAL 30 CR` (ADR-069); a build already in flight is a card that reports and
offers nothing, `SHIPYARD L1 RISING AT DOTHAN` / *Done T5 - 20 credits spent*, `MAP` its only
action (ADR-070). An **answered proposal** reads `ACCEPTED` or `DECLINED` in
the past tense on the button that was pressed, outlined blue like a queued build, with the other
button still imperative and still tappable, so changing an answer before the lock is one tap
(ADR-068); each card answers the offer its digest entry names, so two open offers no longer share
one card's buttons. Tapping a card focuses the system it is about, and **the systems it is
about that the card's own tap does not reach are named chips** — `HOLLIS`, `NYX`, four at most and
then `+n`, each focusing that system (ADR-081). A card carries no button labelled `MAP`: the one
that pointed where the card body already points was one tap drawn twice, and the ones that pointed
somewhere else said nothing about where.

**Overflow (ADR-061, ADR-080).** An **actor card is collapsed** unless it is the open one: dot,
title, `3 EVENTS`, the verdict box and the whole action row, with the per-event lines behind a tap on
its 22px title band; one card is open at a time. A stack that still does not fit **scrolls, by whole
cards**: a wheel notch over the column, a drag that began on it, or `PageDown` moves the top down —
the wheel and a key by one card and a screenful respectively, a drag when it has banked 44 pixels.
Nothing is ever drawn part-way off the top.

The 22px band at the foot of the column stays as the tap route and **says what is below it**:
`27 MORE · 1 BATTLE ›` — the hidden count and the worst hidden thing, a battle outranking its own
kind because the verdict is what makes it one — or `END` in dim ink when the bottom is on the
screen, with `‹ PREV` on the left once there is anything above. Its two halves move by a screenful.
The scroll and the open card reset when a new digest arrives. The leading card carries the standing
moves (ADR-056) and is at the top of the stack rather than pinned to the screen.

*Differs / not built:* no tick stamp on the right of a plain event card; no highlighted card
variant; buttons that do not fit the column are dropped rather than wrapped. **The sheets and the
locks rail still do not scroll** (ADR-052 option C, which ADR-080 amends for this column only). The handoff's four card-level
signals (`REBUILD LANE`, `PLAN ROUTE`, `WITHDRAW`, `HOLD FIRE`) are the signal sheet's rows instead
(ADR-039).

**A capture is news for three ticks (ADR-082).** `CAPTURED Tn` is drawn under a system for three
ticks after it changed hands and then not at all, in the new owner's colour when that is the viewer
and in red otherwise. Red on this screen is what you lost; a rival taking a system from another
rival still reads red, which is the half `SnapshotSystem` cannot yet answer. The legend is not drawn
while a sheet is open, because the two share the bottom strip of the pane.

**Map (centre, `MapRender.cpp`).** Per `DESIGN-GUIDELINES.md` "Map": camera, grid, stars, lanes
with costs, routes, the sealed region, systems and fleets depth-sorted, `MAP - FOCUS: PELL`, and the
legend. Drag orbits. The not-drawn list is in the guidelines and in `README.md` item 6.

**What is standing where (ADR-079).** A system holding fleets wears a garrison badge beside its
name, one per owner, carrying that owner's total ships there — `DOTHAN 10` filled in your blue,
`Xander 16` washed in the holder's colour. **Tapping your own badge is how a parked fleet is
ordered from the map**: one fleet there opens `MOVE FLT n - PICK LANE` directly, several open
`FLEETS AT DOTHAN - PICK ONE` first, and the guard ADR-077 put on `OpenFleet` means neither can ever
name a fleet the server has on a lane. A rival's badge focuses and nothing more. At the lock a badge
is focus-only, as a locks-rail row is.

**A fleet's route (ADR-055).** Every fleet under way draws a line of travelling dots from its origin
to its destination in its owner's colour; the marker sits at `(cost − left) / cost` of the lane.
**The marker keeps clear of both ends (ADR-059):** held a label's half-width from either system, so
an ordered-but-unlocked move (progress zero) is not drawn on the node it is leaving; a lane too short
for that draws it in the middle. `01-orders-queued.png` is the ordered-but-unlocked case and
`01-fleet-under-way.png` the same fleet a tick out.

**Locks rail (260px, right) — orders no order, links to all of them (`DrawLocksRail`).** `LOCKS T8`
/ `UNLOCKED` (amber); one line of help; sections `FLEETS n` — **grouped by where they are**
(ADR-086): a muted band per system holding something of yours, `DOTHAN · 10 SHIPS`, with rows
`FLT 1 · 10` under it (the id muted, the count primary) and no right-hand column unless the fleet is
the incumbent, when it carries `+DEF` in blue; everything in transit under one `UNDER WAY` band as
`FLT 8 · 4 → PELL` with `T9`. The band's total is the same number the map's garrison badge carries.
`BUILDS 2 AVAIL - 26 CR` (queued rows `SHIPYARD L1 - PELL` / `QUEUED -20`, then a row per build already rising,
`SHIPYARD L1 - DOTHAN` / `T5` in muted ink — the form FLEETS uses for a fleet under way, ADR-070 —
a `- 6 cr left at the lock -` line, `- nothing queued -` otherwise), `SIGNALS 9 TO SEND ›` (rows
`SENDING`, a concede in red), `PROPOSALS 1 OPEN` (rows `P3 LANE` / `3 TICKS` amber). Footer
`ALL LOCK TOGETHER` + the countdown.

**Rows are links (ADR-060).** A BUILDS row opens the build sheet for the system its build is on; a
FLEETS row opens the fleet's destination picker, and is **the only way to move a parked fleet**
(ADR-077) — a fleet the server already has on a lane takes no order, so its row focuses where it is
going instead; a PROPOSALS row focuses the far end of the lane the offer is about. None of them gives an order. The
row under the pointer is filled with `HOVER_FILL`, and only a row that is a target draws one. At the
lock and in a finished match every row is focus-only.

*Differs:* the `SIGNALS` section's queued rows are not links, and the trade-lane `PROPOSE` row is
drawn from `BuildRow::isTradeLane`, which nothing sets.

**Sheets (ADR-052).** The four panels — build, destination, signal, replay — are one component
(`DrawPanel`), drawn as a sheet against the bottom of the map pane, 44px rows, six at most, a
seventh reported. No mockup exists for any of them; the captures are `01-build-sheet.png`,
`01-destination-sheet.png`, `01-signal-sheet.png`, `01-signal-sheet-armed.png`, `07-replay.png`
and, for a sheet left open across the lock, `06-at-lock-sheet.png`.
- **Build** (`BUILD - DOTHAN`): opened by tapping a system you hold, or by a queued BUILDS row on
  the rail; when the queue has already taken credits, a wrapped line under the header says what the
  sheet is priced against — *Priced against the 26 credits left after the 20 already queued, not the
  46 in hand.* — amber when a row on the sheet is dim for want of them and `TEXT_DETAIL` when it is
  only a note, and absent with an empty queue (ADR-078). It lists that system's buildings
  and nothing else (ADR-058) — `Shipyard L1 - Dothan`, what that level pays and how long it takes
  under it (`+2 ships a tick - 1 tick`), and `20 CR` on the right (ADR-070), or `QUEUED`, or
  `20 CR - NEED 7 MORE` dim; a system already building shows that one row instead — `It cannot take
  another order until this lands` / `DONE T5`, not a target (`01-build-rising.png`) — and a system
  with both built says `NOTHING LEFT TO BUILD HERE`; a system you do not hold opens no sheet and
  only focuses. Tapping a row queues or unqueues (ADR-053's guard refuses what the purse cannot
  cover).
- **Destination** (`MOVE FLT 1 - PICK LANE`): opened by the fleet's FLEETS row on the rail, by its
  system's garrison badge on the map (through the fleet list where a system holds several,
  ADR-079), by `MOVE` on a card, or by tapping a marker of your own that has not departed yet; never for a fleet
  the server has on a lane, because the lock refuses a second order on one (ADR-077). One row per
  lane out of **where the fleet stands**: owner square,
  `PELL`, `UNCLAIMED` / `YOURS` / `P3` with the hostile ships standing there and `+DEF` appended
  (`P3 · 11 +DEF`, ADR-063), then `· CAPITAL` / `· CONTESTED`, and `2 TICKS · ETA T9` on the right —
  the lane cost and the arrival tick as two facts. Picking a row orders the move (drawn at progress
  zero until the lock) and closes the sheet. *Not built:* the verdict under the right-hand column
  (`YOU WIN` / `HOLD` / `YOU LOSE`). It needs a preview per candidate destination on
  `SnapshotFleet`; the client must not compute one (ADR-063).
- **Fleet list** (`FLEETS AT DOTHAN - PICK ONE`): opened by a garrison badge on a system holding
  more than one of your fleets (ADR-079) — a badge totals ships, and a picker has to be about one
  fleet. Rows are `FLT 3` with the engagement preview under it and `3 SHIPS` on the right, blue
  square, and tapping one opens that fleet's destination sheet. A garrison that left at the lock
  leaves `NOTHING STANDING HERE ANY MORE`.
- **Signal** (`SIGNAL - PICK ONE`): opened from the rail's `SIGNALS` header; the six kinds of
  ADR-039 as rows with `SENDING` / `TAP AGAIN TO CONFIRM` on the right; `Concede` always last,
  needing two taps, under a 22px `CONCEDE` band of its own and said in red from the first tap
  (ADR-064) — the band counts against the six-row cap and is dropped, never the row, when the sheet
  is full; `Concede` alone when the empire has met nobody — the `NOTHING TO SAY YET` row is
  written for an empty list, and the list is never empty because `Concede` is always on it; fourteen
  rows offered and the rest counted.
- **Replay** (`REPLAY TICK 7`): the stub of screen 07.

**Behaviour.** The countdown is live and rounds up (`00:00:00` and `LOCKED` are the same event,
ADR-039); at zero the page is screen 06. Every handled tap sends the whole order set at once and
the server keeps the latest, which is what makes "editable until the lock" true without the client
knowing when the lock is. Accept/decline is an order. The page redraws only on a change (ADR-047)
and continuously while a fleet is under way (ADR-055).

**Finished match (`01-finished.png`; no mockup).** After `VIEW LAST DIGEST` on the MATCH FINISHED
dialog: top bar `MATCH ENDED --:--:--`, rail header `FINAL` / `MATCH ENDED` in red, help *The match
is over. This is what you finished with.*, footer `NOTHING MORE LOCKS` / `T30 FINAL`, every control
inert, the digest carrying no standing moves.

**Code.** `LockstepClient/MainPage.{h,cpp}` (bar, two rails, sheets, hits), `DigestView.{h,cpp}`
(ranking, grouping, standing moves, the delta), `MapRender.{h,cpp}`, `MapView.h`, `MatchState.h`
(the view model); `Lockstep/SnapshotView.cpp` turns the snapshot and the digests into it and
composes each event's actions. Tap tests in `Tests/LockstepTests/TapTests.cpp`.

## 02 · Share tick — **not built** (no capture; the mockup that was its target is in the design file and the history)

The handoff: a 480×640 PNG from `SHARE TICK` — a map crop centred on the top event's systems, a
16px first-person headline, one amber sentence, the four-cell delta and the standings line.

Nothing of it exists: no button on the top bar, no renderer for the card, no clipboard path. If it
is built it goes to the clipboard and never to a file (ADR-034 §2 — a client that writes a file
would be R13's first client-side exception). Whether it should exist before anyone has played a
match is ADR-034's third open question, still open.

## 03 · Join — **built** (`03-join.png`)

`JoinPage`: the sky (a fixed camera on the same star field as the map) with a 480px column centred:
`LOCKSTEP` at 2× and `JOIN A MATCH - ONE SEAT PER TOKEN`; a card with a `SERVER` field (`LAST USED`
on its label line), a `TOKEN` field (masked; `SHOW` toggles), two lines saying a token names a seat
and not a person, a dashed `SEAT` box reading `NOT YET CONFIRMED`, and `JOIN >` filled once both
fields have text (outlined `CONNECTING` while a connection is in flight). Footer: `ALSO: --join
SERVER TOKEN`. The caret starts in the token field; Tab switches, Enter is JOIN. A failure on this
machine (`No answer from that server.`) is one red line beside the button; anything the server
answered with is a screen 05 dialog over the card, with `BACK` and `EDIT TOKEN` because there is a
field to go back to.

Pre-filled: the host's own client gets `127.0.0.1:7341` and the first token it generated (ADR-036);
a `--join`/`--token` client that was refused or could not connect gets what the command line said.
Nothing is remembered between runs — R13 leaves the client nothing to write — so `LAST USED` is a
label, not a fact.

*Differs / not built:* the seat preview (`4 OF 12 · YOU ARE BLUE`) is set only on the frame the
Welcome arrives, and the screen hands off on that frame, so it is never read; the footer's match
summary (`MATCH 0419 · TICK 46 · LOCKS 02:14:09`) has a setter and no caller. Both need a Welcome
that carries more than a seat (`Protocol.h`) and a screen that waits to show it.

**Code.** `LockstepClient/JoinPage.{h,cpp}`, `NeuronClient/TextField.h`, `KeyboardInput`;
`RunJoinScreen` in `Lockstep/Lockstep.cpp`. Tests: `JoinPageTests.cpp` (typing and taps).

## 04 · Connection lost — **built** (`04-connection-lost.png`)

**A BANNER, not a modal** (ADR-085). `ConnectionDialog::Kind::Lost` draws a 44px amber-bordered
band under the top bar spanning all three columns, with no scrim: `CONNECTION LOST - RECONNECTING IN
2S` (or `- BACK ONCE ALREADY - RETRYING IN 1S`, or `BACK 4 TIMES`) on the left, `QUIT` outlined and
`RETRY NOW` filled on the right. It swallows only its own two buttons.

**The board under it stays live and gives no order.** Every control that reaches the wire goes inert
by the same path the lock uses (`MainPage::OrdersEditable`) — the digest's buttons dim, and every
row that would compose an order is not a target at all; reading a card, focusing a system, orbiting
the map and opening a sheet all still work, because none of them reaches a socket. A sheet open
across the drop wears an `OFFLINE` chip where `LOCKED` goes and an amber line saying *The link is
down. Nothing you tap here is sent; the board is yours to read.*

The loop retries every two seconds; `RETRY NOW` skips the wait. A reconnect passes through
`Connecting` and the banner stays up for it (ADR-043). Past zero the band's countdown line reads
`T10 locked while you were away.` rather than counting down to nothing.

*Differs:* the handoff promised *unlocked orders are kept locally and re-sent*; nothing re-sends
them, so the dialog says the true, smaller thing (ADR-038). Whether pending edits should survive a
drop is ADR-038's open question.

## 05 · Connection states — **built** as one component (`05-connecting.png`, `05-refused-unknown-token.png`, `05-refused-seat-in-use.png`, `05-waiting-for-the-host.png`, `05-match-finished.png`; CONNECTION LOST is `04-connection-lost.png`; NOT UNDERSTOOD is not captured)

CONNECTING covers all three parts of getting in — looking the host name up, reaching the peer, and
waiting to be welcomed — and does not distinguish them (ADR-071). A name is resolved on a worker
thread and a numeric host never starts one, so this dialog is usually past the lookup within a frame.

Seven states of `ConnectionDialog`, chosen in one place from the connection and the state so two can
never be true at once (`Lockstep.cpp`, the match loop; `RunJoinScreen` for the join screen):

- `CONNECTING` neutral — the server, *Reaching the server.* then *Sending token - waiting for
  Welcome.* (ADR-043: the connect no longer blocks, so the dialog covers the whole wait), *Waiting
  3s.*, `CANCEL`. No progress bar.
- `REFUSED - UNKNOWN TOKEN` red — `BACK` · `EDIT TOKEN` (filled) on the join screen; `QUIT` alone
  in the match loop, where there is nothing behind the dialog to go back to. (`RefusalReason::UnknownToken`)
- `REFUSED - SEAT IN USE` red — `BACK`/`QUIT` · `RETRY` (filled), which opens a new connection
  because a refusal is final. (`AlreadyConnected`)
- `REFUSED - NOT UNDERSTOOD` red — `BACK`/`QUIT`. Not in the handoff. (`Malformed`)
- `WAITING FOR THE HOST` blue — *You are in. Seat 02 is yours.* and why there is nothing to show;
  `QUIT`. Not in the handoff: it is what a joiner sees between being welcomed by a lobby and the
  host's `ENTER MATCH` (ADR-038; it replaced a fake match).
- `MATCH FINISHED` neutral — *This match has ended…* and `4 OF 6 - SCORE 1284 - LEADER P3 1610`;
  `QUIT` · `VIEW LAST DIGEST` (filled), which dismisses it once and shows 01 in its finished state.
  (`MatchHeader::finished`; `RefusalReason::MatchFinished` is on the wire and no server sends it.)
- `CONNECTION LOST` — screen 04, the same component.

*Not built:* the handoff's `WELCOME · SEAT 4 OF 12` dialog (match, tick, lock time, *3 digests
waiting since you last looked*, `ENTER >`). A welcome goes straight to the match; the seat is the
join screen's business and the missed ticks are the digest header's. Dedicated-server model: there
is no host-left state.

**Code.** `LockstepClient/ConnectionDialog.{h,cpp}`. Tests: `ConnectionDialogTapTests` in
`TapTests.cpp` — every button pressed, the scrim swallowing, no `BACK` without a screen behind.

## 06 · At lock — **built** (`06-at-lock.png`)

Applies to 01 when the countdown reaches zero (ADR-039): top bar `T8 LOCKED 00:00:00` with the
countdown in grey rather than amber; digest header right side `T8 PENDING` in amber; every action
button outlined dim and inert (`MAP` still focuses), no standing moves; the rail's header a filled
grey `LOCKED` chip, its help line amber — *Resolving T8. Controls return with the new digest.
Anything you tap now is an order for T9.* — its `SIGNALS` header `LOCKED` and not a control, its
footer `LOCKED TOGETHER` / `T8 RESOLVING`; its rows focus-only (ADR-060). **An open sheet stays
open** (ADR-065): every row dim and not a target, the same filled grey `LOCKED` chip in its header
clear of the `X`, and the rail's lock sentence repeated under the header in amber. `X` and `CANCEL`
still close it. Ends when the next state arrives and the page is rebuilt from it — including the
sheet, which reopens when its system is still yours or its fleet still exists, and closes when it is
not. When the server is on time this screen lasts under a second; it is what a player sees when the
server is late, which is when it matters.

`FormatCountdown` rounds up so that `00:00:00` and the rail reading `LOCKED` are the same instant
(the bug ADR-039 found by photographing the client).

## 07 · Replay — **stub** (`07-replay.png` is the stub as built)

Built: `▶ REPLAY T7` on the top bar opens a sheet titled `REPLAY TICK 7 - NOT YET WIRED` listing
`1. LOCK` … `6. DIGEST` as dim, untappable rows, and `CANCEL`. That is all, and the title is where
it says so: the seventh note row that used to carry that sentence was clipped by the six-row cap and
read as `+1 MORE THAN THIS SHEET CAN SHOW`, so it was removed.

Not built: the step-through — done/current/upcoming phases, one line per `PhaseRecord`, the
rule-of-thumb line, `PREV`/`NEXT` and the `[<] [>] [D]` keys, and the board rendered as of the current
phase behind it. The snapshot carries no phase records, so the wire comes first
(`Design/Plans/4X-02-ServerAndClient.md` still lists pointing *Replay tick N* at the server's
`TickLog`). When it is built it is a sheet, not the handoff's 600px centred panel (ADR-052).

## 08 · Missed digests — **partial** (`08-missed-digests.png` is the capture)

Built (ADR-044): the server keeps eight ticks of digest per player and sends the whole backlog on
arrival; the match loop concatenates the digests newer than the last tick it drew, oldest first,
into one list, and when the new state is two or more ticks past that the header reads `SINCE YOU
LOOKED · T4 → T7` with a `3 TICKS` chip and the four-cell delta counted over that list. Ranking and
grouping run over the whole window, so a rival busy across three ticks is one actor card.

**Repeats are folded (ADR-062).** In that window only, a run of consecutive events of the same kind
about the same subject becomes one card: `PRODUCTION +18 - T6 > T9`, detail from the newest of them
(`154 credits in hand`), every control the run offered carried across once. Never a contact, a
capture, a proposal or anything carrying a verdict, and a title's trailing digits are summed only
when a `+` or `-` introduces them. The delta box still counts the raw digest.

Not built: the per-tick tabs (`T44 · 5 | T45 · 4 | T46 · 7`), the `3 TICKS WAITING · 16 EVENTS`
header, older ticks collapsed at the bottom with `OPEN >`, and marking a tick read. Two limits are
inherent: a restarted client has no memory of what it read (R13) and so opens with the backlog as a
plain `DIGEST - TICK 7`; and a player away longer than eight ticks gets the eight most recent with
a header that counts all of them and nothing that says the rest are gone (ADR-044's open question —
the tabs are where that belongs).

**Code.** `Lockstep/Lockstep.cpp` (the match loop: `drawnTick`, `unreadTicks`),
`LockstepClient/DigestView.cpp` (`DeltaOf`). Tests: `DeltaTests` in `DigestViewTests.cpp`.

## 09 · Seats — **built**, not in the handoff (`09-seats.png`, `09-seats-joined.png`; no mockup)

The host's lobby, after they have joined their own server with the first token it generated
(ADR-036 as amended; ADR-037, 041, 051). A joiner never sees it. On the join screen's sky:
`LOCKSTEP` at 2×, `SEATS - BEFORE THE MATCH STARTS`, and `6 SEATS - 1 OF 6 CONNECTED` on the right;
a 960px console holding a 3×2 grid of seat cards (swatch, `SEAT 01`, `YOU` or the empire name,
the token, a status line, and the three-way `HUMAN | BOT AT T1 | BOT` — one control for one
question, ADR-066, its segments sized to their labels), the practice box under the grid (`FIRST
MATCH?` and an amber `PRACTICE MATCH >` with the two numbers that differ — a tick every two minutes
against six hours, five bots, thirty ticks), a detail panel for the selected seat on the right (what
a token is; the token with `COPY` to the clipboard and `NEW TOKEN`; whose seat it is; for a bot seat
`HOW IT PLAYS` with three styles; for a human seat one line saying what the card's setting does),
and a footer with the summary (`WAITING FOR SORNE, TAMSIN - 4 OF 6 HERE`
in amber, `ALL 6 SEATS CONNECTED - YOU ARE SEAT 01` in blue) or the refusal to the last tap,
`FILL WAITING WITH BOTS`, and `ENTER MATCH >` — filled only when every seat is connected, a bot, or
marked to be taken over; Enter is the same. `CONNECTED` is live from the server this process runs.

Decisions on this screen: six seats and no `EMPTY` (ADR-036 amendment 3); `TAKE SEAT` removed —
the host's seat is the token their client presented (ADR-041); entering is what turns a seat nobody
came to into a bot (ADR-037); one three-way rather than a card toggle and a panel pair (ADR-066);
`PRACTICE MATCH` hands every seat but the host's and anybody connected to a bot and enters at once
(ADR-051). The screen is not throttled (ADR-047's open question).

**Code.** `Lockstep/SeatsPage.{h,cpp}` (in the executable because it names `BotPolicy`, ADR-050),
`RunSeatsScreen` in `Lockstep.cpp` (the clipboard). Tests: `SeatsPageTapTests` in `TapTests.cpp`.
