# ADR-034 — The UI v2 handoff, and the four things it left open

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Owner, asked directly on receiving `Design/UI/`.
**Supersedes:** —

---

## Context

`Design/UI/` arrived on 2026-09-11: eight screens at 1280×720, guidelines, a per-screen spec and a
work plan, superseding the v1 `Design/Screens/`. Its central idea is an inversion of the built
layout — **the digest becomes the order surface**, every event carrying its own actions, and the
right-hand rail demoting to a read-only summary of what goes in at the lock. Columns move from
`300 | fill | 330` to `400 | fill | 260`.

Most of it is a specification a build session can simply follow. Four things were not, because each
contradicted something already decided or already written, and a build session that resolved them by
inference would have been guessing at an owner's intent. They were put to the owner and answered.

This ADR records the four answers together rather than as four thin ADRs, because they arrived
together as answers to one document and each is short.

## Decisions

### 1. The map keeps its camera. The projection in the guidelines is not the spec.

`DESIGN-GUIDELINES.md` gives the map projection as `d = y/560`, `s = 0.5 + 0.65·d`,
`sy = 60 + 440·(0.3·d + 0.7·d²)`. That is the authored curve of ADR-016 — a fixed tilted plane with
no eye position, no field of view and no way to move the viewpoint. **ADR-017 replaced it with a
real orbit camera precisely because the turntable could not**, and ADR-032's sky is a sphere of
directions projected through that camera, which a fixed curve cannot do at all.

So the formula is taken as **a description of how the static mockups were rendered**, not as a
requirement. `OrbitCamera` stands, framed by default to sit close to the reference view, with drag
and zoom live.

**The cost is stated plainly:** once the player moves the camera, the map will not be pixel-identical
to its PNG, and no screenshot comparison against `screens/` can be exact for the map pane. Every
other pane is fixed and can be compared exactly.

### 2. The share card goes to the clipboard and never to a file.

Screen 02 exports a 480×640 PNG of a tick. R13 says the executable ships alone, and it was amended
twice on this same day: a process **acting as the server** may write a match store (ADR-024) and an
instrumentation log (ADR-030), and a client writes nothing.

A share card written beside the executable would have been R13's third exception and its first
client-side one. It is not worth that: the destination of a share card is a conversation, and the
clipboard is already the shortest path there. **Clipboard only. R13 stands unamended**, and this
paragraph is the record that a file was considered and declined rather than never thought of.

If a later session wants a file — a folder of tick cards to look back over, say — that is a new
decision and needs R13's third exception with its own ADR.

### 3. The interface layer gains a minimal text field. ADR-014 is amended, not superseded.

Screen 03 wants a server address and a token typed in. ADR-014's interface layer has **no text input
of any kind**: it draws rectangles, ellipses and 8×8 glyphs, and takes taps and drags.

One focusable field is added: printable ASCII, backspace, a blinking caret, a character limit. No
selection, no clipboard paste, no IME, no multi-line. Everything a field normally has and this one
does not is a thing nobody has needed yet.

**The reason it is worth the machinery now rather than later is in the blueprint: the product is a
mobile client** (owner decision, 2026-09-11). A phone has no command line to fall back on, so a
client that can only be joined by `--join host:port --token x` is a client that cannot ship. The
keyboard behind it is a separate problem and is not solved here.

### 4. No reference fixture. Screens render what the server sends.

`PROMPT.md` asks for a fixture seeded to the reference data — T46→47, twelve players, sixty-one
systems, Halvorsen leading on 1,610 — so that every screen renders like its PNG on first run. The
owner's answer was to **ignore that request**.

Step 2 of `4X-02` retired `MatchFixture` deliberately, and reinstating it would put a second source
of truth about what a match looks like back into the tree — one that drifts from the resolver the
moment a rule changes, and that has to be maintained to keep drifting screens honest.

**The consequence is a real cost and should be expected rather than discovered.** No screen will look
like its PNG on first run; the digest will show whatever a fresh match produces, which at T0 is one
event and no contacts. Reviewing a screen against its design means playing or replaying a match to a
state that exercises it. `--tick <seconds>` (ADR-030's rehearsal flag) is what makes that cheap.

## Consequences

**Work can start on `PROMPT.md` without a second round of questions.** Its steps 1–7 are otherwise
unambiguous.

**Two of its steps change shape.** Step 6's join screen gains a text field rather than being a
confirmation screen; step 7's share card loses its file path. Step 5's missed-digest tabs stay one
deep, as `PROMPT.md` itself instructs, and the ADR to retain N digests on the server is still owed.

**`DESIGN-GUIDELINES.md` is corrected in place.** It is not an ADR — it is a reference, and
`Design/README.md` §2 says a reference that turns out wrong was a bug in the document. Its map
section now points at `OrbitCamera` and keeps the formula as the mockups' provenance.

**The v1 references in older ADRs are left alone.** ADR-014, 015, 016, 017, 020, 021, 022 and 027 all
cite `Design/Screens/README.md`, which no longer exists. They are Accepted and therefore immutable
except for their status line, so the redirect goes in `Design/README.md` where a reader will find it,
and not into eight ADR bodies.

## Open questions

**Whether the digest's client-side ranking can disagree with the server's.** ADR-020 has the digest
sorted by a severity the resolver carries. The new guidelines specify a consequence order, actor
grouping and a `LEADER` tag computed on the client. If the two orders ever differ, the player sees
one and the log records the other.

**What the text field does about a soft keyboard.** On Windows there is a hardware one. On the
mobile client the blueprint names, there is not, and nothing here says who raises it.

**Whether `SHARE TICK` should exist before anyone has played a match.** It is the one screen with no
in-game consumer, and its value is a guess about what people will want to show each other.
