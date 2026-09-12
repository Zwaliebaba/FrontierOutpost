# ADR-047 — The client rests

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, on the codebase review's §4 item 7, 2026-09-12.

---

## Context

The client presented at vsync, forever, for a screen that changes four times a day. The review calls
it the one real performance cost in a tree that is otherwise resolving ticks in microseconds, and it
is: a laptop playing a three-week match spent three weeks rendering the same picture sixty times a
second.

## Decision

**The match screen is redrawn when it changes.** A change is a state from the server, a tap, a drag,
a dialog appearing or going, the connection coming or going, and **the countdown's displayed second
turning over** — which is what keeps the idle rate at one frame a second rather than none. The
second is taken from `ceil(secondsToLock)` because `FormatCountdown` rounds up, so it is the number
on the bar rather than the float behind it.

When nothing has changed the loop sleeps eight milliseconds and presents nothing. The swap chain
still holds the last frame; there is nothing to put there.

**The join screen is not throttled.** It has a blinking caret, it is on screen for seconds rather
than weeks, and a screen somebody is typing into is the last place to start skipping frames.

## What it found

**`WAITING FOR THE HOST` was shown to players in a started match.** The test for "nothing has
arrived" was `drawnTick == 0`, and a match that has started but not yet locked its first tick sends
a perfectly good state whose tick *is* zero. So a player looking at a drawn galaxy, with their fleet
and their capital on it, was told the host had not started yet. It flashed past at a rehearsal tick
and would have sat there for six hours at the authored one.

It became visible because throttling made a long tick worth testing: at `--tick 30` the screen holds
still, and holding still is when you read it. `everHadState` is the fix.

## Verified

A client joined to a live match, left alone: **0 ms of CPU over ten seconds**, with the countdown
still running on screen. Before this it was a frame every 16 ms.

## Open questions

**The seats screen is not throttled either.** It polls the server for who has arrived and redraws
every frame to show it. It is a lobby somebody is watching, so the cost is bounded by their patience
rather than by a three-week match, but it is the same fix if it ever matters.
