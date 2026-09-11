# ADR-038 — The connection says what it is doing, and the reference fixture goes

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, "go ahead" on the connection dialogs and the waiting-for-the-host screen,
2026-09-12.

**Implements:** SCREENS.md 04 (connection lost) and 05 (connection states), and PROMPT.md step 4's
"no reference fixture".

---

## Context

Every state a connection can be in that is not *playing* had no drawing at all.

- A refusal **after** the join screen handed off put up a `MessageBoxA` — a Win32 dialog in a game
  that draws its own everything — and then returned `EXIT_FAILURE`. The player was told their token
  was wrong and the window that could fix it was closed in the same breath.
- A **dropped link** showed nothing. `MatchConnection` has had a spaced reconnect loop for as long
  as it has existed; it ran silently underneath a screen that still looked live, and the player's
  taps went into a client that could not send them.
- A client welcomed by a **lobby that had not started its match** was sent no state at all — the
  server has no match to make one from — so the composition root drew `MakeReferenceMatch()`: the
  design sheet's twelve-player mid-match, with invented empires, an invented galaxy and an invented
  countdown. That was defensible when the comment was written, because the fixture was only up "for
  the fraction of a second between connecting and being welcomed". It stopped being true the day
  ADR-036's amendment put the lobby *before* the match: a player who joins before the host taps
  `ENTER MATCH` looks at the fixture for as long as the host takes to arrange the seats.

The third is the worst of them, and it is worth being precise about why: **a fake match is not
distinguishable from a real one.** A blank screen is confusing; a convincing screen showing an
empire that does not exist is a lie the player will act on.

## Decision

**One `ConnectionDialog` for six states rather than six dialogs.** They differ in a title, a colour,
a paragraph and two buttons. What varies is passed in as `Facts` every frame, so nothing is stale —
which is what makes screen 04's "next attempt in 2s" and its lock countdown live, and that liveness
is the point of screen 04.

| Kind | Tone | Buttons |
| --- | --- | --- |
| `Connecting` | neutral | CANCEL |
| `Waiting` | blue | QUIT |
| `Refused · UNKNOWN TOKEN` | red | BACK · **EDIT TOKEN** |
| `Refused · SEAT IN USE` | red | BACK · **RETRY** |
| `Refused · NOT UNDERSTOOD` | red | BACK |
| `Lost` | amber | QUIT · **RETRY NOW** |
| `Finished` | neutral | QUIT · **VIEW LAST DIGEST** |

**`BACK` exists only where there is something behind.** The join screen sets `canGoBack`; the match
loop does not, and every `BACK` becomes `QUIT` there. A button that returns to a screen this process
left twenty minutes and one match ago is not a button.

**The dialog swallows every tap it is over, the map included.** A tap reaching the board behind a
CONNECTION LOST dialog would be an order edit the client cannot send, and the player would have no
way to tell which of their taps counted.

**The reference fixture is deleted**, not merely unused. `MatchFixture.h/.cpp` anticipated this in
its own header — "the day the server sends a digest, this becomes a test fixture rather than the
boot path" — but the executable ships alone (R13) and has no test project, so there is nothing for
it to be a fixture *for*. Two hundred and seventy-eight lines of invented match data that nothing
reads is not an asset. The client now boots with an empty `MatchState` and the `Waiting` dialog over
it.

**A `--join` that is refused lands on the join screen, not in the match loop.** `Open` succeeding
only means the TCP connection was made; the refusal arrives on the first `Pump` after it. The
command-line path now waits to be told yes or no before deciding which screen to open, and a refusal
opens screen 03 — pre-filled with what the command line asked for — with the refusal dialog over it.
The connection is deliberately *not* reset first, so the player is told why they are looking at a
field. Resetting first was tried and is worse than the message box it replaced: it drops the player
on a blank form with no explanation.

**A `--join` that cannot reach anything does the same**, for the same reason. It used to be a second
`MessageBoxA` and an `EXIT_FAILURE`.

## Two promises this deliberately does not make

**Screen 04's paragraph is "your unlocked orders are kept here and re-sent when the link returns".
Nothing re-sends them.** Orders tapped while the dialog is up reach `MatchConnection::SendOrders`,
which drops them because the status is not `Playing`, and no code on either side of the socket
replays them afterwards. `MatchConnection`'s own comment claimed the reconnect "will send the
current rail anyway"; it never did, and that comment has been corrected — a lie in a comment is a
defect, and this one would have been read as permission to skip the work. The dialog says the true,
smaller thing instead: orders already sent are on the server and still count, and anything tapped
now is not.

**Screen 05's CONNECTING dialog cannot appear while the socket is being opened.** `Socket::Connect`
is deliberately blocking — a refusal is reported at the call rather than surfacing later as a socket
that never produces anything — so a wrong address freezes the window for the OS connect timeout and
nothing pumps, draws or reads a CANCEL button during it. The dialog covers the `Greeting` phase,
which is real and non-blocking: the socket is open, the `Hello` is sent, and no `Welcome` has come
back.

## Consequences

**`RefusalReason::MatchFinished` is on the wire and no server sends it.** The end of a match is
found in the snapshot (`MatchHeader::finished`) and reaches the dialog as `Kind::Finished`. This is
recorded rather than fixed because it is the sort of thing somebody wires up in good faith and then
discovers the dialog already covers.

**`MatchConnection` gained `Reset`, `Reopen` and `RetryNow`**, which are three different retries
because the states they come from are different. A refusal is final by design — `Pump` stops pumping
a refused connection, because an unknown token will still be unknown in three seconds — so the only
way back from one is a new connection (`Reopen`). A lost link is already being retried on a timer,
and `RETRY NOW` only means "do not make me wait for it" (`RetryNow`).

## What was verified, and what was not

The workstation was locked for this pass, which means no window can be brought to the foreground and
**no synthetic tap can land anywhere**. `PrintWindow` still captures, so every state reachable
without a tap was driven end to end against a real server and photographed: REFUSED · UNKNOWN TOKEN
(both over the match screen and over the join screen), REFUSED · SEAT IN USE, CONNECTION LOST with a
live retry countdown, and MATCH FINISHED with real standings after a forty-eight-tick bot match. The
`Waiting` dialog was observed giving way to a live match the moment the first state arrived.

**Not verified by running: the buttons.** BACK, EDIT TOKEN, RETRY, QUIT and VIEW LAST DIGEST are
wired and compiled and have never been pressed. The harness that would press them
(`Build/TapRehearsal.ps1` and its successors) needs an unlocked desktop. Whoever next has one should
press all five before trusting them.

## Open questions

**Whether unlocked orders should survive a disconnection.** The reference sheet says yes and the
game says no. Doing it properly means holding the pending order set outside `MatchState`, because a
reconnect rebuilds the whole screen from the server's snapshot and would overwrite local edits
before they could be re-sent.

**Whether `Socket::Connect` should be non-blocking with a deadline.** It would make the CONNECTING
dialog cover the whole wait and give CANCEL something to cancel. It is a `NeuronCore` change with
its own tests and did not belong in a screen pass.
