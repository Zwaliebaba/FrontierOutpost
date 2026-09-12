# Screens

Per screen: what the build draws, what the 2026-09-11 handoff asked for that it does not, and where
the code is. Status is as of 2026-09-12. Numbers 01–08 are the handoff's; 09 was added by ADR-036
and sits between 03 and 01 in the flow (`README.md` draws it). "Not built" is stated as such, and
nothing below is in the present tense unless the tree does it.

## 01 · Main page — **built** (`01-main-page-5a.png` is the mockup)

**Top bar (44px, `MainPage::DrawTopBar`).** `LOCKSTEP` · `M0007 - D2/21 - 6 PLAYERS - 26 SYSTEMS`
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
Header `DIGEST - TICK 7` with `5 EVENTS` on the right, or `SINCE YOU LOOKED - T4 > T7` with an
amber `3 TICKS` chip and the delta box when ticks resolved unseen (see 08). Then cards in consequence
order, each with its actions on its own row: an event card (dot, uppercased title, wrapped detail,
verdict box when your fleet is flying into a contact, actions), or an actor card grouping a rival's
≥2 events (`P3 - LEADER 1,610`, `3 EVENTS`, one line per event, ranked by the worst). Actions are
composed per event (ADR-057): `ACCEPT`/`DECLINE` on a proposal, `REDIRECT FLT 1` on a contact your
fleet reaches, a priced build on a claimed system or a production line, `MAP` on anything about a
system. When no card offers a real control the standing moves go on the leading card — a priced
`BUILD` and up to two `MOVE FLT n` (ADR-056); before the first lock the digest is one card,
`NOTHING HAS HAPPENED YET`, carrying them. Build buttons show `- QUEUED` (outlined blue) and
`- NEED 7 MORE` (dim, inert) per ADR-053. Tapping a card focuses the system it is about; `MAP`
focuses the system the action names.

*Differs / not built:* no tick stamp on the right of a plain event card; no highlighted card
variant; the actor card is always expanded; buttons that do not fit the column are dropped rather
than wrapped; **the digest does not scroll** — cards past y=720 are cut. The handoff's four
card-level signals (`REBUILD LANE`, `PLAN ROUTE`, `WITHDRAW`, `HOLD FIRE`) are the signal sheet's
rows instead (ADR-039).

**Map (centre, `MapRender.cpp`).** Per `DESIGN-GUIDELINES.md` "Map": camera, grid, stars, lanes
with costs, routes, the sealed region, systems and fleets depth-sorted, `MAP - FOCUS: PELL`, and the
legend. Drag orbits. The not-drawn list is in the guidelines and in `README.md` item 6.

**A fleet's route (ADR-055).** Every fleet under way draws a line of travelling dots from its origin
to its destination in its owner's colour; the marker sits at `(cost − left) / cost` of the lane.
**The marker keeps clear of both ends (ADR-059):** held a label's half-width from either system, so
an ordered-but-unlocked move (progress zero) is not drawn on the node it is leaving; a lane too short
for that draws it in the middle.

**Locks rail (260px, right) — read-only (`DrawLocksRail`).** `LOCKS T8` / `UNLOCKED` (amber); one
line of help; sections `FLEETS n` (rows `FLT 1 10 > PELL` with `T9`, or `FLT 1 10 HOLD DOTHAN` with
`HOLD`, `+DEF` in blue when it is the incumbent), `BUILDS 2 AVAIL - 26 CR` (queued rows
`SHIPYARD - PELL` / `QUEUED -20`, a `- 6 cr left at the lock -` line, `- nothing queued -`
otherwise), `SIGNALS 9 TO SEND >` (rows `SENDING`, a concede in red), `PROPOSALS 1 OPEN` (rows
`P3 LANE` / `3 TICKS` amber). Footer `ALL LOCK TOGETHER` + the countdown.

*Differs:* **rows are not tappable** — the handoff's "tapping a row jumps to the event that owns it"
is not built; the `SIGNALS` header is the rail's only control (it opens the signal sheet). The
trade-lane `PROPOSE` row is drawn from `BuildRow::isTradeLane`, which nothing sets.

**Sheets (ADR-052).** The four panels — build, destination, signal, replay — are one component
(`DrawPanel`), drawn as a sheet against the bottom of the map pane, 44px rows, six at most, a
seventh reported. No mockup exists for any of them.
- **Build** (`BUILD - DOTHAN`): opened by tapping a system you hold; lists that system's buildings
  and nothing else (ADR-058) — `Shipyard - Dothan` with `20 CR`, or `QUEUED`, or
  `20 CR - NEED 7 MORE` dim; a system with both built says `NOTHING LEFT TO BUILD HERE`; a system you
  do not hold opens no sheet and only focuses. Tapping a row queues or unqueues (ADR-053's guard
  refuses what the purse cannot cover).
- **Destination** (`MOVE FLT 1 - PICK LANE`): opened by `MOVE`/`REDIRECT` on a card or by tapping
  your fleet's marker; one row per lane out of where the fleet is or is going: owner square,
  `PELL`, `UNCLAIMED` / `YOURS` / `P3` with `- CAPITAL` / `- CONTESTED` appended, and `2 TICKS - ETA
  T9` on the right — the lane cost and the arrival tick as two facts. Picking a row orders the move
  (drawn at progress zero until the lock) and closes the sheet.
- **Signal** (`SIGNAL - PICK ONE`): opened from the rail's `SIGNALS` header; the six kinds of
  ADR-039 as rows with `SENDING` / `TAP AGAIN TO CONFIRM` on the right; `Concede` always last and
  needing two taps; `NOTHING TO SAY YET` when the empire has met nobody; fourteen rows offered and
  the rest counted.
- **Replay** (`REPLAY TICK 7`): the stub of screen 07.

**Behaviour.** The countdown is live and rounds up (`00:00:00` and `LOCKED` are the same event,
ADR-039); at zero the page is screen 06. Every handled tap sends the whole order set at once and
the server keeps the latest, which is what makes "editable until the lock" true without the client
knowing when the lock is. Accept/decline is an order. The page redraws only on a change (ADR-047)
and continuously while a fleet is under way (ADR-055).

**Finished match (no mockup).** After `VIEW LAST DIGEST` on the MATCH FINISHED dialog: top bar
`MATCH ENDED --:--:--`, rail header `FINAL` / `MATCH ENDED` in red, help *The match is over. This is
what you finished with.*, footer `NOTHING MORE LOCKS` / `T30 FINAL`, every control inert, the digest
carrying no standing moves.

**Code.** `LockstepClient/MainPage.{h,cpp}` (bar, two rails, sheets, hits), `DigestView.{h,cpp}`
(ranking, grouping, standing moves, the delta), `MapRender.{h,cpp}`, `MapView.h`, `MatchState.h`
(the view model); `Lockstep/SnapshotView.cpp` turns the snapshot and the digests into it and
composes each event's actions. Tap tests in `Tests/LockstepTests/TapTests.cpp`.

## 02 · Share tick — **not built** (`02-share-tick-5b.png` is the target, if it stays one)

The handoff: a 480×640 PNG from `SHARE TICK` — a map crop centred on the top event's systems, a
16px first-person headline, one amber sentence, the four-cell delta and the standings line.

Nothing of it exists: no button on the top bar, no renderer for the card, no clipboard path. If it
is built it goes to the clipboard and never to a file (ADR-034 §2 — a client that writes a file
would be R13's first client-side exception). Whether it should exist before anyone has played a
match is ADR-034's third open question, still open.

## 03 · Join — **built** (`03-join-3a.png` is the mockup)

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

## 04 · Connection lost — **built** (`04-connection-lost-3b.png` is the mockup, on the v1 page)

`ConnectionDialog::Kind::Lost` over the live 01 (the scrim dims it; the top bar behind reads
`RECONNECTING`). Amber: `CONNECTION LOST` · *The server stopped answering.* · *Reconnecting - next
attempt in 2s.* (or *back 3 time(s) already - next attempt in 1s.*) · *Orders you already sent are
on the server and still count. Anything you tap while this is up is not sent.* · *The tick still
locks in 00:01:40 whether or not you are back.* — live, from the page's countdown. Buttons `QUIT` ·
`RETRY NOW` (filled). The loop retries every two seconds; `RETRY NOW` skips the wait. A reconnect
passes through `Connecting` and the dialog stays up for it (ADR-043). The dialog swallows every tap,
the map included.

*Differs:* the handoff promised *unlocked orders are kept locally and re-sent*; nothing re-sends
them, so the dialog says the true, smaller thing (ADR-038). Whether pending edits should survive a
drop is ADR-038's open question.

## 05 · Connection states — **built** as one component (`05-connection-states-3b.png` is the mockup)

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

## 06 · At lock — **built** (`06-orders-locked-3c.png` is the mockup, on the v1 layout)

Applies to 01 when the countdown reaches zero (ADR-039): top bar `T8 LOCKED 00:00:00` with the
countdown in grey rather than amber; digest header right side `T8 PENDING` in amber; every action
button outlined dim and inert (`MAP` still focuses), no standing moves; the rail's header a filled
grey `LOCKED` chip, its help line amber — *Resolving T8. Controls return with the new digest.
Anything you tap now is an order for T9.* — its `SIGNALS` header `LOCKED` and not a control, its
footer `LOCKED TOGETHER` / `T8 RESOLVING`; any open sheet closed. Ends when the next state arrives
and the page is rebuilt from it. When the server is on time this screen lasts under a second; it is
what a player sees when the server is late, which is when it matters.

`FormatCountdown` rounds up so that `00:00:00` and the rail reading `LOCKED` are the same instant
(the bug ADR-039 found by photographing the client).

## 07 · Replay — **stub** (`07-replay-3d.png` shows the target content on a superseded panel)

Built: `▶ REPLAY T7` on the top bar opens a sheet titled `REPLAY TICK 7` listing `1. LOCK` …
`6. DIGEST` as dim, untappable rows, and `CANCEL`. That is all. The sheet's own seventh row, `NOT
YET WIRED TO A RESOLVED TICK`, is clipped by the six-row cap and appears as `+1 MORE THAN THIS SHEET
CAN SHOW` — a small defect worth a line here so nobody reads it as a real overflow.

Not built: the step-through — done/current/upcoming phases, one line per `PhaseRecord`, the
rule-of-thumb line, `PREV`/`NEXT` and the `[<] [>] [D]` keys, and the board rendered as of the current
phase behind it. The snapshot carries no phase records, so the wire comes first
(`Design/Plans/4X-02-ServerAndClient.md` still lists pointing *Replay tick N* at the server's
`TickLog`). When it is built it is a sheet, not the handoff's 600px centred panel (ADR-052).

## 08 · Missed digests — **partial** (`08-missed-digests-3e.png` shows the target on the v1 layout)

Built (ADR-044): the server keeps eight ticks of digest per player and sends the whole backlog on
arrival; the match loop concatenates the digests newer than the last tick it drew, oldest first,
into one list, and when the new state is two or more ticks past that the header reads `SINCE YOU
LOOKED - T4 > T7` with a `3 TICKS` chip and the four-cell delta counted over that list. Ranking and
grouping run over the whole window, so a rival busy across three ticks is one actor card.

Not built: the per-tick tabs (`T44 · 5 | T45 · 4 | T46 · 7`), the `3 TICKS WAITING · 16 EVENTS`
header, older ticks collapsed at the bottom with `OPEN >`, and marking a tick read. Two limits are
inherent: a restarted client has no memory of what it read (R13) and so opens with the backlog as a
plain `DIGEST - TICK 7`; and a player away longer than eight ticks gets the eight most recent with
a header that counts all of them and nothing that says the rest are gone (ADR-044's open question —
the tabs are where that belongs).

**Code.** `Lockstep/Lockstep.cpp` (the match loop: `drawnTick`, `unreadTicks`),
`LockstepClient/DigestView.cpp` (`DeltaOf`). Tests: `DeltaTests` in `DigestViewTests.cpp`.

## 09 · Seats — **built**, not in the handoff (no mockup)

The host's lobby, after they have joined their own server with the first token it generated
(ADR-036 as amended; ADR-037, 041, 051). A joiner never sees it. On the join screen's sky:
`LOCKSTEP` at 2×, `SEATS - BEFORE THE MATCH STARTS`, and `6 SEATS - 1 OF 6 CONNECTED` on the right;
a 960px console holding a 3×2 grid of seat cards (swatch, `SEAT 01`, `YOU` or the empire name,
the token, a status line, `HUMAN | BOT`), the practice box under the grid (`FIRST MATCH?` and an
amber `PRACTICE MATCH >` with the two numbers that differ — a tick every two minutes against six
hours, five bots, thirty ticks), a detail panel for the selected seat on the right (what a token is;
the token with `COPY` to the clipboard and `NEW TOKEN`; whose seat it is; for a bot seat `HOW IT
PLAYS` with three styles; for a human seat `IF STILL WAITING AT T1 LOCK` — `BOT TAKES OVER` |
`SEAT GOES CUSTODIAN`), and a footer with the summary (`WAITING FOR SORNE, TAMSIN - 4 OF 6 HERE`
in amber, `ALL 6 SEATS CONNECTED - YOU ARE SEAT 01` in blue) or the refusal to the last tap,
`FILL WAITING WITH BOTS`, and `ENTER MATCH >` — filled only when every seat is connected, a bot, or
marked to be taken over; Enter is the same. `CONNECTED` is live from the server this process runs.

Decisions on this screen: six seats and no `EMPTY` (ADR-036 amendment 3); `TAKE SEAT` removed —
the host's seat is the token their client presented (ADR-041); entering is what turns a seat nobody
came to into a bot (ADR-037); `PRACTICE MATCH` hands every seat but the host's and anybody
connected to a bot and enters at once (ADR-051). The screen is not throttled (ADR-047's open
question).

**Code.** `Lockstep/SeatsPage.{h,cpp}` (in the executable because it names `BotPolicy`, ADR-050),
`RunSeatsScreen` in `Lockstep.cpp` (the clipboard). Tests: `SeatsPageTapTests` in `TapTests.cpp`.
