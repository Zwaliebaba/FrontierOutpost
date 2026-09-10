# ADR-009 — A wheel notch arrives as `WM_POINTERWHEEL`, so the `WM_MOUSEWHEEL` fallback comes out

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session, after measuring on hardware.
**Supersedes:** — (refines ADR-008; see *What this changes elsewhere*)

---

## Context

ADR-008 gave the camera a stepped zoom driven by the wheel and by pinch, and hedged in one place
(quoted from the version of 2026-09-10 that this ADR answered; ADR-008 was rewritten in place later
that day for the map camera and no longer carries the hedge or the "absent anchor" below):

> `EnableMouseInPointer` is documented to route mouse input into the pointer family, which should
> make a wheel notch arrive as `WM_POINTERWHEEL` — but that is the one part of the claim this
> project has not confirmed against real hardware […] a handler for only the first would leave zoom
> quietly not working at all if the claim is wrong.

That hedge was explicitly conditional on an unknown, and the reason the unknown persisted was
circumstantial rather than technical: the workstation was locked for most of the session, the
window manager drops injected pointer messages, and there is no touch digitizer on this machine.
None of that is a property of the code.

The workstation was unlocked on 2026-09-10 and the question became answerable.

## The measurement

With `EnableMouseInPointer(TRUE)` in force and the game window focused, a real wheel notch
delivered through the input stack (`mouse_event` with `MOUSEEVENTF_WHEEL`, which goes through the
same path as a physical wheel) arrives at the window procedure as:

```
message 0x024E   ==   WM_POINTERWHEEL
```

Three notches, three deliveries, no `WM_MOUSEWHEEL` at all. The zoom responded: the lit area of the
frame went 6,410 → 3,263 → 6,431 physical pixels as the scale went 8 → 6 → 4 → 6, which matches the
figures ADR-008 recorded from injected messages to within measurement noise.

This is a fact rather than a decision, and it is recorded here rather than in `Design/Reference/`
only because the decision below depends on it and splitting the two would separate the evidence
from the conclusion.

## Options considered

### A. Keep both cases

Harmless, two lines, and it would keep the project's automated harness able to drive zoom by
posting `WM_MOUSEWHEEL` while the workstation is locked — which is genuinely useful, because
posted pointer messages are dropped and that is the only remaining way to exercise zoom unattended.

It is also dead code in every configuration the game actually runs in, and the argument for it is
now an argument about test tooling rather than about the program. Shaping shipping code around a
scratch harness is the wrong way round.

### B. Remove the `WM_MOUSEWHEEL` case

Leaves exactly one input path, which is what MVP-01 §2 asks for — the same rule that says
`WM_LBUTTONDOWN` is only allowed "if you have proven `EnableMouseInPointer` is unavailable". The
proof has now gone the other way for the wheel, so the parallel is exact.

Costs the unattended-zoom test path described above.

## Decision

**B.** `PointerInput::HandleMessage` handles `WM_POINTERWHEEL` and not `WM_MOUSEWHEEL`.

The test that asserted the classic message also worked is replaced by one asserting it is ignored,
so the absence is pinned rather than merely true today.

The harness now drives the wheel through the real input stack instead of by posting, which needs
an unlocked desktop — an accepted loss, and the honest one: a test that could only ever exercise a
path the game does not use was not testing the game.

## Consequences

**What this makes easy.** One wheel path, proven, with nothing to keep in sync.

**What this makes hard.** Zoom can no longer be exercised while the workstation is locked, because
the only injectable wheel message is the one that is no longer handled. Every other automated check
in this project survives a locked session; this one does not.

**What it costs.** If `EnableMouseInPointer` ever fails — the call is checked and its failure is
already traced — the game now has no wheel *and* no click, rather than a wheel and no click. That
is not a regression worth guarding against: a game you cannot click is not usable because the wheel
still works.

**What it forecloses.** Nothing. Should the measurement ever stop holding — a Windows version that
routes the wheel differently, a remote-desktop or accessibility layer that synthesises the classic
message — this is a new ADR and two lines.

## What this changes elsewhere

- **Code:** `NeuronClient/PointerInput.cpp` loses the `WM_MOUSEWHEEL` case.
  `Tests/NeuronClientTests` pins its absence.
- **Design/:** this **refines ADR-008 rather than superseding it**, exactly as ADR-008 refines
  ADR-003. Everything ADR-008 decided — the even zoom levels, the two producers and one intent, the
  absent anchor, the pinch ratio — is unchanged. The single paragraph it revises is the one that
  handled both wheel messages, and the *reason* that paragraph gave for existing is the thing this
  ADR removes. `Design/README.md` §4 makes an Accepted ADR immutable, so ADR-008 is left alone.
- **AGENTS.md:** no change.

## Open questions

A real pinch is still untested, and this ADR does not change that: it needs a touch digitizer,
which this machine does not have. ADR-008's open questions about the range of zoom levels and the
1.25 pinch ratio stand unanswered for the same reason.
