# ADR-027 — Twelve owner colours, and *you* are always blue

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** **Owner decision, 2026-09-11**, choosing twelve authored colours over the two alternatives recorded below. Implemented in `4X-02-ServerAndClient.md` Step 2.
**Supersedes:** —

---

## Context

`MatchState::Owner` was a four-value enum — `You`, `Halvorsen`, `Sorne`, `Neutral` — and it was
right for what it was built against. `Design/Screens/README.md` draws one specific mid-match with
three empires in it, and ADR-014 built that screen. A match has six to twelve players.

The enum's real limitation was not its size. It was that **"you" is not a property of a system.**
The same capital is blue on its owner's screen and somebody else's colour on everybody else's, and
with the answer baked into the state there was nowhere for the difference to live. Step 8 made this
unavoidable by giving every player their own snapshot (ADR-022): twelve players now genuinely see
twelve different pictures of the same board.

The palette constrains the answer. `MatchState.cpp` puts it plainly: the owner colours **are** the
semantic colours. Blue is you and also *accept* and also *a trade lane*; amber is a rival and also
*warning* and also *the countdown*; coral is a rival and also *loss*. That economy is deliberate — a
player learns three colours instead of six — and it means owner colours cannot be chosen freely,
because each one is already saying something else.

And the medium is unforgiving: an 8×8 bitmap font, no anti-aliasing, nodes a handful of pixels
across, on black.

## Options considered

### A. Four semantic colours: you, ally, rival, neutral

Colour by *relationship to the viewer* rather than identity. Reads instantly, never runs out, and
survives any player count.

Rejected, and the reason is the late game. The one-pager says what a player learns over time:
*"Late: how to read other humans — who over-extends, who bluffs, who is quietly winning."* A map
that renders every rival identically deletes that. You cannot notice that one empire is quietly
winning if every empire is the same colour.

It also has nothing to say about "ally", since the game has **no enforced treaties** — a trade lane
partner is not an ally, and colouring them as one would assert a relationship the rules do not have.

### B. A smaller palette plus a per-node glyph

Six colours and a one-character initial on each node. The 8×8 font already draws glyphs at this
size, so identity would be exact rather than approximate.

Rejected on the medium. A node is a few pixels across with a name label already beside it and a
stem beneath it; adding a character inside or next to it crowds the map at exactly the player count
where it was supposed to help. The reference map is legible because each node carries one label,
and this would give it two.

### C. Twelve authored colours

One per player index, chosen once. Identity is exact, the map stays as clean as the reference, and
nothing new is drawn.

The cost is that twelve hues distinguishable at 8px on black is genuinely hard, and some pairs will
be closer than anyone would like.

## Decision

**C, with blue reserved and the ordering doing real work.**

**`Owner` becomes `OwnerId`, a plain player index**, with `NOBODY` for unheld. `OwnerColor(owner,
viewer)` takes both, because the answer depends on who is looking.

**Blue is always *you* and never anybody else.** It is not in the rival table at all. A viewer sees
their own systems blue and every rival in that rival's own entry — so a given empire looks the same
to everybody who is not them, and no rival can ever be mistaken for you.

**The rival table is twelve entries and the order is not arbitrary.** The galaxy is a ring and the
generator places capitals in player order around it, so **adjacent player ids are adjacent
empires**. The sequence alternates warm and cool — amber, teal, coral, violet, lime, orchid, rust,
jade, rose, mint, sand, plum — so the colours a player most needs to tell apart, their own two
neighbours, are furthest apart in hue. Distant empires may look similar, and that costs less.

That is the honest mitigation: **the map does not need twelve globally distinguishable colours, it
needs local ones.** Claiming twelve perfectly distinct hues at this size would be a claim nobody
could keep.

**Out-of-range ids wrap rather than clamp.** A player id past the table is a bug rather than a
state; wrapping gives two empires the same colour, where clamping would give every one of them the
last colour. The first is confusing and the second is unreadable.

**The legend shows what this player can see**, not all twelve: you, plus up to four rivals with
territory on the map, plus the two lane entries. Twelve swatches would fill the bar with colours
for empires nobody has met. Past four, the map is its own legend — every system carries a name and
tapping one says who holds it.

**`PlayerBadge::label` is a string, and it is `"P2"` today.** Names belong to the server and arrive
with identity in `4X-02` Step 3. One place changes when they do.

## Consequences

**The design reference still renders exactly as drawn.** `MatchFixture` assigns Halvorsen player id
0 (amber) and Sorne id 2 (coral) — the colours the reference shows them in — and makes the viewer
id 3, which is also fourth of twelve, the placement the reference prints. The fixture is now a test
fixture rather than the boot path, and it still reproduces the drawing pixel for pixel.

**Phase 0 will find the close pairs, and that is the plan.** Six players use ids 0–5: amber, teal,
coral, violet, lime, orchid, which are comfortably distinct. Twelve is where sand and amber, or
violet and plum, may prove too close. Moving a colour is a one-line change and this ADR is the
record of why the order matters when somebody does.

**Colour-blindness is not addressed.** Twelve hues is a bad starting point for it and the one-pager
says nothing about accessibility. Named honestly here rather than left to be discovered: the
mitigation, if it is needed, is the glyph option B rejected, and it would be a real redesign of the
node rather than a palette change.

## What this changes elsewhere

`MatchState` gains `viewer` and `players`; `SystemNode`, `Fleet`, `Lane`, `Proposal` and `BuildRow`
gain the ids they need to be named in an order. `Design/Screens/README.md`'s token list describes
three empires and now describes the general rule.

Two of the client's types were renamed in the same change, for a different reason: `Match` became
`MatchHeader` and `FleetOrder` became `FleetStance`, because `GameLogic` has types of those names
and the adapter is the first file to include both. The client's were the vaguer ones.

## Open questions

**What a player is called before identity exists.** "P2" is a placeholder that will be on screen
for the whole of Step 2, and a legend reading *YOU, P3, P5* is worse than the reference's *YOU,
HALVORSEN, SORNE* in every way except being true.

**Whether the legend should show rivals a player has met but cannot currently see.** It shows
rivals with territory on the map right now, so an empire you fought last week and have lost sight
of drops off the legend while its colour stays in your memory of the map.
