# Screens

Per screen: what the build draws, what the 2026-09-11 handoff asked for that it does not, and where
the code is. Status is as of 2026-09-13, and each heading names its capture in `screens/`; every
capture ADR-069 and ADR-070 touched was retaken that day from the Debug build carrying them, and the
join and connecting screens are 2026-09-12's, which nothing since has moved. The handoff's mockups
are no longer in the tree (`README.md` says where they went). Numbers 01–08 are the handoff's; 09
was added by ADR-036 and sits between 03 and 01 in the flow (`README.md` draws it). "Not built" is
stated as such, and nothing below is in the present tense unless the tree does it.

## 01 · Main page — **built** (`01-main-page.png` is the capture, tick 11 of a six-seat match)

**Top bar (44px, `MainPage::DrawTopBar`).** `LOCKSTEP` · `D2/21 · 6 PLAYERS · 26 SYSTEMS`
· spacer · `T8 LOCKS` + the countdown in the 16px display cut in amber (ADR-084) · `46 CR −20` —
the purse, then in blue what this tick's queue has already committed of it, drawn only when
something is queued (ADR-087) · `SCORE 1,284` + chip `4TH / 6` — blue when you lead, **amber when
your place is worse than on the last digest this client drew**, outline otherwise (ADR-091) ·
`LDR P3 1,610` · `▶ REPLAY T7` **only under `--dev`**, because its sheet is a stub and a control
that says its own screen is unfinished teaches that the buttons here may not work. The left sentence is measured against what the right block leaves
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
order, each with its actions on its own row: an event card (dot, **sentence-cased** title in the 16px
display cut — `Battle at Ulme`, `Shipyard L1 rising at Dothan` — ADR-099, wrapped detail,
verdict box when your fleet is flying into a contact, actions), or an actor card grouping a rival's
≥2 events (`P3 - LEADER 1,610`, `3 EVENTS`, one line per event, ranked by the worst). Actions are
composed per event (ADR-057): `ACCEPT`/`DECLINE` on a proposal, `REDIRECT FLT 1` on a contact your
fleet reaches, a priced build on a claimed system or a production line, `MAP` on anything about a
system. When no card offers a real control the standing moves go on the leading card — a priced
`BUILD` and up to two `MOVE FLT n` (ADR-056); before the first lock the digest is one card,
`Nothing has happened yet`, carrying them. Every button is a **28px box with its number in a
second cell** and wears one of the four control states (ADR-111): a queued build is Committed,
`MINING STATION L2 JANDAL | −30`, and says `TAKE BACK | +30` under the pointer; one the purse
cannot cover is Inert, dashed, `NEED 7 MORE` in amber, and is not a target. The label names the
LEVEL it would build (ADR-069); a build already in flight is a card that reports and
offers nothing, `Shipyard L1 rising at Dothan` / *Done T5 - 20 credits spent*, `MAP` its only
action (ADR-070). An **answered proposal** reads `ACCEPTED` or `DECLINED` in
the past tense on the button that was pressed, outlined blue like a queued build, with the other
button still imperative and still tappable, so changing an answer before the lock is one tap
(ADR-068); each card answers the offer its digest entry names, so two open offers no longer share
one card's buttons. Tapping a card focuses the system it is about, and **the systems it is
about that the card's own tap does not reach are named chips** — `HOLLIS`, `NYX`, four at most and
then `+n`, each focusing that system (ADR-081). A card carries no button labelled `MAP`: the one
that pointed where the card body already points was one tap drawn twice, and the ones that pointed
somewhere else said nothing about where.

**A title is a sentence and the cut it is set in is mono** (ADR-099, ADR-102). Uppercase stays for
chips, section headers and status words — `QUEUED`, `CAPTURED`, `3 EVENTS`, `FLEETS`. The face is the
one bend in ADR-074's *sentences are sans* on any screen, and it is a fact about the bake rather than
a preference: the 16px display cut exists in Plex Mono only (ADR-073), so a title at title size has
nowhere sans to go. An actor card's title is capitals throughout without being a shout — `P3 - LEADER
1,610` is a name and a status word.

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
variant; buttons that do not fit the column are dropped rather than wrapped. **Nothing scrolls
except the three columns of rows** (ADR-052 option C, which ADR-080 amends for the digest, ADR-101
for the locks rail and ADR-112 for the place sheet's body). The handoff's four card-level signals
(`REBUILD LANE`, `PLAN ROUTE`, `WITHDRAW`, `HOLD FIRE`) are the signal sheet's rows instead
(ADR-039).

**The top bar's purse says what is committed (ADR-087).** `46 CR −20` — the purse, then in blue
what this tick's queue has already taken of it, drawn only when something is queued. It is the same
`QueuedBuildCost()` the place sheet's sentence explains and the affordability guard refuses by, so
the bar, the rail's `- 26 cr left at the lock -` and the sheet cannot disagree.

**A capture is news for three ticks (ADR-082).** `CAPTURED Tn` is drawn under a system for three
ticks after it changed hands and then not at all, in one of three inks (ADR-088): your colour when
you took it, red when it was taken from you, and the new owner's colour at 0.7 when two rivals
traded it. Red on this screen is what you lost and nothing else — the snapshot carries
`capturedFrom` so the map can tell the third case from the second. The legend is not drawn while a
sheet is open, because the two share the bottom strip of the pane.

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

**Locks rail (260px, right) — what goes in at the lock, and the places it is about
(`DrawLocksRail`).** Header `ORDERS · T8` / `UNLOCKED` (amber); one line of help — *What goes in when
the clock hits zero. Tap a row to open the place it is about.*

**`ORDERS` is one list and every row is one shape** (ADR-113): an 8px owner square, the title in
mono Medium, the place or the count in muted, the number in blue, and a 44×44 `X` that takes the
order back. `SHIPYARD L1 · DOTHAN · −20 X` and `FLT 1 → FAROE · 10 SHIPS · T1 X` are the same row.
**The cell says a capital `X` and not `×`** because `×` was never baked and would draw nothing at
all (ADR-114, §Font below).
The order within it is what this lock will take (queued builds, then queued moves), then what an
earlier one already did (rising builds `T14`, then fleets under way `T9`, both muted and with **no**
`X`), then **a dim row behind a dashed square per fleet with no move at all** — `FLT 1 · NO MOVE ·
DOTHAN` — which is the one thing this column never used to say. Under them, when the queue has taken
credits, `80 CR LEFT AT THE LOCK`. The row's body opens the place; the `X` cell does not.

**`PLACES`** replaces `FLEETS` and `BUILDS`: one row per system you hold — a 10px disc in the
owner's colour, `DOTHAN`, `+6 · 10 SHIPS` muted, and `1 ORDER` in blue or `—` in dim — and the row
opens that place. Then `SIGNALS 9 TO SEND ›` (rows `SENDING`, a concede in red) and
`PROPOSALS 1 OPEN` (rows `P3 LANE` / `3 TICKS` amber), both unchanged. Footer `ALL LOCK TOGETHER`
+ the countdown.

**The sections scroll and the header and footer do not (ADR-101).** 44px rows (ADR-100) put a played
empire's four sections past the bottom of a 260px column, so everything between the help line and the
footer is a band that moves: a wheel notch over the rail moves it by a row, and **a 44px page band at
the foot of that band is the control a finger uses** — the digest's, in the same place, saying the
same kind of thing. `3 MORE · SIGNALS ›` on the right, naming the first section below the fold, or
`3 MORE ›` when there is no section down there, or `END` in dim ink at the bottom; `‹ UP` on the left
once there is anything above. Its halves move by a bandful less one row. **A row not wholly inside
the band is not drawn and registers no hit** — `ShapeRenderer` has no clip rectangle, so a
half-scrolled row would paint its divider over the help line. The scroll is **not** reset when a tick
lands, unlike the digest's: this column is a summary of the same empire tick after tick.

**Rows are links, and one cell is not (ADR-060, ADR-113).** An `ORDERS` row and a `PLACES` row open
the place they are about; a fleet the server already has on a lane takes no order, so its row
focuses where it is going instead; a `PROPOSALS` row focuses the far end of the lane the offer is
about. **The `X` on an `ORDERS` row is the one control on this rail that gives an order** — it takes
one back, which is the same tap the tile or the fleet row on the sheet would take. The row under the
pointer is filled with `HOVER_FILL`, and only a row that is a target draws one. At the lock and in a
finished match every row is focus-only and no `X` is drawn.

*Differs:* the `SIGNALS` section's queued rows are not links.

**Sheets (ADR-052).** The four panels — place, destination, signal, replay — are one component
(`DrawPanel`), drawn as a sheet against the bottom of the map pane: 44px header, one wrapped help
line, a body, a 44px bar. The body is 44px rows, six at most and a seventh reported —
**except the place sheet's, which is a 2×2 grid of 96px tiles over a short list of fleets**
(ADR-107, ADR-112). **A sheet swallows every tap it is over** (ADR-112): it records its own
rectangle before its controls, so a tap on the space between two of them no longer falls through to
the map. No mockup exists for any of them; the captures listed here are `01-build-sheet.png`,
`01-destination-sheet.png`, `01-signal-sheet.png`, `01-signal-sheet-armed.png`, `07-replay.png`
and, for a sheet left open across the lock, `06-at-lock-sheet.png` — **the first and the last of
those are of a sheet that no longer exists** and are owed a retake (`Design/UI/README.md`).
- **Place** (`DOTHAN`): **the one sheet an order is given on** (ADR-112), and the one whose body is
  a grid rather than a column (ADR-107). Opened by tapping a system you hold, by its garrison badge,
  by a digest button that names it, or by a rail row about it; it is about that system and nothing
  else (ADR-058), and a system you do not hold opens no sheet and only focuses.
  - **Header.** A 10px disc in the owner's colour, the system's name in the display cut, then a muted
    clause saying what the place IS — `YOURS · +6 A TICK · CAPITAL`, dropped whole rather than
    clipped when the status slot leaves no room for it. Right of that, inboard of the 44px `X`, one
    of three things: the filled grey `LOCKED` / `OFFLINE` chip (ADR-065, ADR-085), or — when
    something is rising there — an outlined blue 22px chip reading `RISING · DONE T14`, or the
    purse, `66 CR` with ` −40` in blue when this tick's queue has taken something (ADR-087), so the
    number a dim tile is priced against is on the sheet rather than 400 pixels away.
  - **Help line.** One wrapped sans line and three sentences compete for it, in this order: the
    rail's lock sentence in amber (ADR-065); *Xerev cannot take another order until this lands. Two
    of three ticks are in.* when something is rising there, the count in words below ten (ADR-070);
    then *Priced against the 26 credits left after the 20 already queued, not the 46 in hand.* when
    the queue has taken credits — amber when that is why a tile is dim and `TEXT_DETAIL` when it is
    only a note, absent with an empty queue (ADR-078).
  - **Tiles.** One 284×96 tile per thing the system can build, in the role order mining station,
    shipyard, bastion, trade lane, packed — **an empty slot is not drawn**, so today's two-building
    system is one row of two tiles and the sheet is 200px of the 676px pane. Each carries an icon, a
    title that names the step once (`Shipyard L1 → L2`, `Bastion L1`, `Mining station L2 rising`), a
    level ladder, what the level pays and how long it takes (`+3 ships a tick · 2 ticks`, ADR-070),
    and a bottom line — `25 CR` / `1 CR LEFT AFTER`, `QUEUED −40` / `TAP TO TAKE BACK`, `45 CR` /
    `NEED 19 MORE` dim, or `2 OF 3 TICKS` / `DONE T14` over a 3px progress bar. The whole tile is
    the target and tapping one queues or unqueues (ADR-053's guard refuses what the purse cannot
    cover).
  - **A system that is building keeps its whole ladder** (ADR-107, amending ADR-070): the rising
    tile, and the tiles for what it could build next drawn inert, priced, and marked `AFTER T14` —
    so a player mid-build still has the prices and the yields to plan against (`01-build-rising.png`).
    None of them is a target; the lock refuses a second construction whatever its kind (ADR-069).
  - A system with everything at its top level says *Both buildings are at their top level.* where
    the grid would be.
  - **Fleets here.** Under a divider and a `FLEETS HERE` band carrying the ships standing there, one
    boxed 44px row per fleet of yours the place holds: an 8px owner square, `FLT 1` in mono Medium,
    `10 SHIPS · HOLDING` muted, and a 28px button at the right — outlined `MOVE ›`, or committed
    `TAKE BACK` once a move is queued, when the row reads `10 SHIPS → FAROE · T1` instead.
    **A fleet belongs to the place it was ordered OFF**, so a move given this tick keeps its row
    rather than vanishing from it (ADR-055, ADR-077). The section is omitted when the place holds
    nothing of yours.
  - **The body is capped at half the pane and scrolls** (ADR-052, ADR-112): past the cap a wheel
    notch or a drag banked to 44 moves it **by blocks** — a band, a tile row, the divider, a fleet
    row — and the whole `FLEETS HERE` section is pinned above the bar when it fits in half the body,
    so the move stays one tap away. There is no page band: this body can be dragged, where the locks
    rail cannot (ADR-101).
  - **The bar says `DONE`, not `CANCEL`.** There is nothing to back out of — what was ordered on it
    is already in, and closing it is finishing.
- **Signal** (`SIGNAL - PICK ONE`): opened from the rail's `SIGNALS` header; the six kinds of
  ADR-039 as rows with `SENDING` / `TAP AGAIN TO CONFIRM` on the right; `Concede` always last,
  needing two taps, under a 22px `CONCEDE` band of its own and said in red from the first tap
  (ADR-064). **The band and the row are pinned below the six and outside the count** (ADR-093),
  immediately above `CANCEL`, so the sheet's six are six real signals and `+N MORE` counts only
  them; `Concede` alone when the empire has met nobody — the `NOTHING TO SAY YET` row is
  written for an empty list, and the list is never empty because `Concede` is always on it; fourteen
  rows offered and the rest counted.
- **Replay** (`REPLAY TICK 7`): the stub of screen 07.

**Move mode (ADR-114; `01-move-mode.png` and `01-move-selected.png` are owed).** A move is chosen
ON the map, not in a list. It is entered by `MOVE ›` on a fleet's row in the place sheet, the
digest's `MOVE FLT 1 | 10 SHIPS`, that fleet's unordered row in the rail's `ORDERS`, a marker of
your own that has not departed, or a garrison badge with **exactly one** of your fleets under it —
which skips the sheet, because a badge totals ships and one fleet is one thing a tap could mean.

The sheet collapses, the digest fades to 55% — **every ink on the column, its BUTTONS' borders and
fills included**, which is a thing a test reads back off the vertices rather than a thing a capture
would have shown — and records no hit, and the map changes meaning: a 44px
banner replaces the `MAP - FOCUS` caption (`MOVE FLT 1` · `10 SHIPS FROM DOTHAN` · *Tap a lit
system.* · `ESC · CANCEL`), the systems **one lane away** light with a pulsing ring and an outlined
`1 TICK · T1` chip under the name, their lanes go blue with marching dashes, every other lane drops
to a hairline, and the origin's badge is outlined. **Nothing else on the map is a target** — an
unreachable system and a rival's garrison are drawn as they always are and record no hit — so a tap
on any of them leaves the mode, as do `ESC` and either `CANCEL`.

**The confirm strip** under it is a sheet in every dimension it shares with one: header
`FLT 1 → FAROE` with `ARRIVES T1 · UNCLAIMED` and `OR PICK FROM THE LIST`; a two-column grid of the
same destinations, six at most, nearest first and then by name (ADR-092); and a bar split 50/50
between `CANCEL` and the filled `SEND 10 SHIPS TO FAROE`, which is **the one filled control on the
screen** while the mode is on and is inert reading `PICK A DESTINATION` until something is lit.
**Lighting is a selection and `SEND` is the order.** On send the mode ends and the sheet does not
come back; the `ORDERS` rail gains `FLT 1 → FAROE · 10 SHIPS · T1 X` and the map draws the dart.

**`--still`** holds the ring's pulse and the lanes' dashes at phase zero, so a capture of this mode
is reproducible; it makes `Animating()` false as well, so the page settles.

**Behaviour.** The countdown is live and rounds up (`00:00:00` and `LOCKED` are the same event,
ADR-039); at zero the page is screen 06. Every handled tap sends the whole order set at once and
the server keeps the latest, which is what makes "editable until the lock" true without the client
knowing when the lock is. Accept/decline is an order. The page redraws only on a change (ADR-047)
and continuously while a fleet is under way (ADR-055).

**Finished match (`01-finished.png`; no mockup).** After `VIEW LAST DIGEST` on the MATCH FINISHED
dialog: top bar `MATCH ENDED --:--:--`, rail header `FINAL` / `MATCH ENDED` in red, help *The match
is over. This is what you finished with.*, footer `NOTHING MORE LOCKS` / `T30 FINAL`, every control
inert, the digest carrying no standing moves.

**Code.** `LockstepClient/MainPage.h`, defined across one unit per pane -- `MainPage.cpp` (creation,
the sentences, the clock), `MainPageTopBar.cpp`, `MainPageMap.cpp`, `MainPageDigest.cpp`,
`MainPageRail.cpp`, `MainPageSheet.cpp`, `MainPageMove.cpp` and `MainPageInput.cpp` (the taps, drags,
notches and keys) -- with the control vocabulary in `Controls.{h,cpp}` (ADR-111) and what the units
share in `MainPageParts.h`; `DigestView.{h,cpp}` (ranking, grouping, standing moves, the delta),
`MapRender.{h,cpp}`, `MapView.h`, `MatchState.h` (the view model); `Lockstep/SnapshotView.cpp` turns
the snapshot and the digests into it and composes each event's actions. Tap tests in
`Tests/LockstepTests/`, one suite per surface over the harness in `Headless.h`: `TapTests.cpp` (the
dialog, the seats, the top bar, the board offline), `DigestTapTests.cpp`, `SignalSheetTapTests.cpp`,
`PlaceSheetTapTests.cpp`, `RailTapTests.cpp`, `MapTapTests.cpp`.

## 02 · Share tick — **not built** (no capture; the mockup that was its target is in the design file and the history)

The handoff: a 480×640 PNG from `SHARE TICK` — a map crop centred on the top event's systems, a
16px first-person headline, one amber sentence, the four-cell delta and the standings line.

Nothing of it exists: no button on the top bar, no renderer for the card, no clipboard path. If it
is built it goes to the clipboard and never to a file (ADR-034 §2 — a client that writes a file
would be R13's first client-side exception). Whether it should exist before anyone has played a
match is ADR-034's third open question, still open.

## 03 · Join — **built** (`03-join.png`)

`JoinPage`: the sky (a fixed camera on the same star field as the map) with a 480px column centred:
`LOCKSTEP` in the 16px display cut and `JOIN A MATCH - ONE SEAT PER TOKEN`; a **283px** card with a
`SERVER` field, a `TOKEN` field **shown by default with `HIDE` beside it** (ADR-095 — a token names a
seat, not a person, and is not authentication), two lines saying so, and `JOIN >` filled once both
fields have text (outlined `CONNECTING` while a connection is in flight). **The fields and the
button are 44px** and the card's height is the sum of what it stacks rather than a number (ADR-100);
`HIDE` stays a 16px word on the `TOKEN` label's line with a 44px hit around it, registered before
the field's so the overlap resolves in its favour. **No `LAST USED` label, no
`SEAT` box and no footer**: the first promised a memory the client cannot have (R13), the second was
never populated, and the third taught a command line to somebody already on the screen that replaces
it — `Design/GETTING-STARTED.md` has the flags. The caret starts in the token field; Tab switches, Enter is JOIN. A failure on this
machine (`No answer from that server.`) is one red line beside the button; anything the server
answered with is a screen 05 dialog over the card, with `BACK` and `EDIT TOKEN` because there is a
field to go back to.

Pre-filled: the host's own client gets `127.0.0.1:7341` and the first token it generated (ADR-036);
a `--join`/`--token` client that was refused or could not connect gets what the command line said.
Nothing is remembered between runs — R13 leaves the client nothing to write, which is why the
`LAST USED` label went.

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
Its buttons are **44px in the card and 24px in the banner** (ADR-100). The card's height is composed
from its button row, so growing the buttons grew the card; the banner is already exactly 44 tall, so
a button drawn at the floor inside it would touch both edges — it stays 24 and its HIT takes the
whole band.

- `REFUSED - UNKNOWN TOKEN` red — `BACK` · `EDIT TOKEN` (filled) on the join screen; `QUIT` alone
  in the match loop, where there is nothing behind the dialog to go back to. (`RefusalReason::UnknownToken`)
- `REFUSED - SEAT IN USE` red — `BACK`/`QUIT` · `RETRY` (filled), which opens a new connection
  because a refusal is final. (`AlreadyConnected`)
- `REFUSED - NOT UNDERSTOOD` red — `BACK`/`QUIT`. Not in the handoff. (`Malformed`)
- `WAITING FOR THE HOST` blue — *You are in. Seat 02 is yours.* and why there is nothing to show;
  `QUIT`. Not in the handoff: it is what a joiner sees between being welcomed by a lobby and the
  host's `ENTER MATCH` (ADR-038; it replaced a fake match).
- `MATCH FINISHED` neutral — *This match has ended.* and **the whole table** (ADR-097): one
  monospace row per player in placement order, `1ST  P6  95` down to `6TH  YOU  35`, the reader's own
  row in their blue. Every player's score and placement is already on the wire in
  `SnapshotStanding`;
  `QUIT` · `VIEW LAST DIGEST` (filled), which dismisses it once and shows 01 in its finished state.
  (`MatchHeader::finished`; `RefusalReason::MatchFinished` is on the wire and no server sends it.)
- `CONNECTION LOST` — screen 04, the same component.

*Not built:* the handoff's `WELCOME · SEAT 4 OF 12` dialog (match, tick, lock time, *3 digests
waiting since you last looked*, `ENTER >`). A welcome goes straight to the match; the seat is the
join screen's business and the missed ticks are the digest header's. Dedicated-server model: there
is no host-left state.

**Code.** `LockstepClient/ConnectionDialog.{h,cpp}`. Tests: `ConnectionDialogTapTests` in
`TapTests.cpp` — every button pressed, the scrim swallowing, no `BACK` without a screen behind.

## 06 · At lock — **built** (`06-at-lock.png`, `06-at-lock-sheet.png`)

Applies to 01 when the countdown reaches zero (ADR-039): top bar `T8 LOCKED 00:00:00` with the
countdown in grey rather than amber; digest header right side `T8 PENDING` in amber; every action
button in the Locked state — filled grey, a 6px square before the label, not a target — except a
focus chip, which reaches no wire and goes on working (ADR-111); no standing moves; the rail's header a filled
grey `LOCKED` chip, its help line amber — *Resolving T8. Controls return with the new digest.
Anything you tap now is an order for T9.* — its `SIGNALS` header `LOCKED` and not a control, its
footer `LOCKED TOGETHER` / `T8 RESOLVING`; its rows focus-only (ADR-060). **An open sheet stays
open** (ADR-065): every row dim and not a target, the same filled grey `LOCKED` chip in its header
clear of the `X`, and the rail's lock sentence repeated under the header in amber. **A place sheet
dims its TILES and its rows in place** (ADR-107, ADR-111): each keeps the border and the icon of
whatever state it is in and all four of its strings go `NEUTRAL_DIM`, and a queued tile drops its
`TAP TO TAKE BACK` rather than dimming an instruction that is no longer true. **The filled grey is a
BUTTON's lock and not a row's**: four light-grey tiles would be the screen inverted rather than gone
quiet (ADR-111). `06-at-lock-sheet.png` is that sheet, which is
what it shows since 2026-09-14; it was the signal sheet before. `X` and `CANCEL`
still close it. Ends when the next state arrives and the page is rebuilt from it — including the
sheet, which reopens when its system is still yours or its fleet still exists, and closes when it is
not. When the server is on time this screen lasts under a second; it is what a player sees when the
server is late, which is when it matters.

`FormatCountdown` rounds up so that `00:00:00` and the rail reading `LOCKED` are the same instant
(the bug ADR-039 found by photographing the client).

## 07 · Replay — **stub** (`07-replay.png` is the stub as built)

Built: `▶ REPLAY T7` on the top bar, under `--dev` only (ADR-091), opens a sheet titled `REPLAY TICK 7` listing
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

**Repeats are folded (ADR-062, ADR-109).** In that window only, every event of the same kind about
the same subject becomes one card: `PRODUCTION +405 - T12 > T20`, detail from the newest of them
(`313 credits in hand`), every control the run offered carried across once. Never a contact, a
capture, a proposal or anything carrying a verdict, and a title's trailing digits are summed only
when a `+` or `-` introduces them. The delta box still counts the raw digest.
**A run is not required to be consecutive and was until 2026-09-15** (ADR-109). It had to be, and
since the window is per-tick blocks — a tick that pays income also reports its buildings and claims —
two repeats were adjacent only on a tick where nothing else happened at all. The fold therefore fired
on the quiet board and not on the one a returning player has: `08-missed-digests.png` carried twenty
unfolded income cards before the change and one folded card after it. **The card keeps the position
of the FIRST of its members**, so nothing around it is reordered.

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
question, ADR-066, its segments sized to their labels **and never under 44px wide**, which is what
`BOT` at three glyphs was, in a **44px** row, ADR-100), the practice box under the grid (`FIRST
MATCH?` and an amber `PRACTICE MATCH >` with the two numbers that differ — a tick every two minutes
against six hours, five bots, thirty ticks), a detail panel for the selected seat on the right (what
a token is; the token with `COPY` to the clipboard and `NEW TOKEN` — both drawn as they were, with
44px hits around them; whose seat it is; for a bot seat
`HOW IT PLAYS` with three styles; for a human seat one line saying what the card's setting does),
and a footer with the summary (`WAITING FOR SORNE, TAMSIN - 4 OF 6 HERE`
in amber, `ALL 6 SEATS CONNECTED - YOU ARE SEAT 01` in blue) or the refusal to the last tap,
`FILL WAITING WITH BOTS` and `ENTER MATCH >`, both drawn at 24px inside the footer's own 44px band
and tappable across the whole of it (ADR-100) — `ENTER MATCH` filled only when every seat is connected, a bot, or
marked to be taken over; Enter is the same. **Disabled it is `TEXT_MUTED` in an `OUTLINE` border**
rather than nearly invisible, and the footer's own sentence is the reason (ADR-096). Under the
summary, always drawn: *Sets every WAITING FOR PLAYER seat to BOT.* — the one control here whose
effect is not in its label, said before it is pressed rather than on a hover a touch device never
has. `CONNECTED` is live from the server this process runs.

Decisions on this screen: six seats and no `EMPTY` (ADR-036 amendment 3); `TAKE SEAT` removed —
the host's seat is the token their client presented (ADR-041); entering is what turns a seat nobody
came to into a bot (ADR-037); one three-way rather than a card toggle and a panel pair (ADR-066);
`PRACTICE MATCH` hands every seat but the host's and anybody connected to a bot and enters at once
(ADR-051). The screen is not throttled (ADR-047's open question).

**Code.** `Lockstep/SeatsPage.{h,cpp}` (in the executable because it names `BotPolicy`, ADR-050),
`RunSeatsScreen` in `Lockstep.cpp` (the clipboard). Tests: `SeatsPageTapTests` in `TapTests.cpp`.
