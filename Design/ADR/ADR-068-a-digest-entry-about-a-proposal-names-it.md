# ADR-068 — A digest entry about a proposal names the proposal, and every open offer is answerable

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session for `Design/Plans/4X-03-PhaseZero.md`, Step 2.
**Supersedes:** —

---

## Context

The digest is the order surface (ADR-034): an event arrives carrying the buttons for what can be
done about it, and a proposal arrives carrying ACCEPT and DECLINE. For that to work the client has
to know **which** proposal a card is about.

It did not. `Lockstep/SnapshotView.cpp` attached the two buttons by walking the open proposals and
comparing `state.proposals[index].id` — a proposal id — against `entry.other.Index()`, which the
resolver fills with **the player who sent it** (`TickResolver.cpp`, the `ProposalReceived` entry).
The two are unrelated numbers that happen to be small integers. What hid it is the second half of
the same condition, `|| state.proposals.size() == 1`: with exactly one offer open the loop takes
the first proposal and is right by construction. Every test, every scripted match and both
rehearsals have run with at most one offer open at a time, so the bug has never been observed.
With two offers open, the buttons land on whichever proposal happens to collide first, or on
neither.

The second half of the same problem is on the client's own side. `MatchState::orders` held
`answeredProposal`, a single index, and `acceptedProposal`, a single flag — **one answer per
tick** — while `OrderSet::answers` is a vector and the resolver applies every element of it
(`TickResolver.cpp`, the answers pass). A player with two offers open could answer one and had to
leave the other until the next lock. The window is four ticks (`MatchRules::proposalWindowTicks`),
so a player who is offered three things in one tick can be made to let one expire, and an expired
offer is reported to its proposer as *ignored* — a tell the design puts weight on
(`Design/blueprint.md` §3), reporting a silence the client manufactured.

Both faults are on the path H1 measures — *do strangers engage in diplomacy* — and Phase 0 is six
people who will have several offers open at once within a day.

## Options considered

### A. Infer the proposal on the client, from the sender and the lane

Costs nothing on the wire. It cannot be made correct: `ShareScouting` and `HoldForTicks` carry no
lane at all, and two offers from the same neighbour are indistinguishable by sender. It also moves
a rule to the client, which is the thing the seam exists to prevent (ADR-025).

### B. Put the proposal on the digest entry

`DigestEntry` already carries the things an entry is *about* — a system, a lane, a fleet, a player
— and the proposal is another one. One `ProposalId` field, one line in the record's single
description (ADR-049), four bytes on every digest entry whether or not it is about a proposal.
The rule is then total: an entry about a proposal names it, in all five of the kinds that can be.

### C. Keep the fallback and cap open offers at one per player

Makes the client's assumption true by changing a rule. Rejected on sight: the four-tick window and
the *ignored* tell are designed around several offers being open, and this would be a game rule
bent to fit a presentation defect.

## Decision

**Every digest entry about a proposal names that proposal.** `DigestEntry` gains
`ProposalId proposal`, described once in `Snapshot.cpp`'s archive visitor and set at all five
kinds that are about one — `ProposalReceived`, `ProposalAnswered`, `ProposalWithdrawn`,
`ProposalIgnored` and `ProposalVoided`, including the two voided entries raised from inside
`OpenTradeLane`, which takes the proposal as a parameter to say it. `other` keeps its meaning: the
counterparty. The client matches on the id and the `size() == 1` fallback is deleted.

**The client holds one answer per open proposal, not one per tick.**
`MatchState::orders.answeredProposal` and `acceptedProposal` become
`std::vector<ProposalAnswer>`, one entry per proposal a player has answered this tick, and
`OrdersOf` emits one `AnswerOrder` for each. Answering the same proposal twice replaces the
answer rather than appending a second. A card whose proposal has been answered draws `ACCEPTED` or
`DECLINED` where its buttons were, and the offer can still be changed until the lock, because an
answer is an order like any other and every order is editable until it locks.

## Consequences

Four bytes are added to every digest entry, including the majority that have nothing to do with a
proposal. That is the price of one description per record (ADR-049): a variant record would need a
discriminator, which costs a byte and a branch on every entry to save four bytes on most of them,
and it would make the archive something a reader has to interpret rather than walk. At the digest
sizes this game produces — a few dozen entries per player per tick — it is not worth the
complexity.

**The wire format changes, so a store written before this cannot be replayed by a build after
it.** No match is being kept: Phase 0 has not started, and a store is per match (ADR-024,
ADR-042). Anyone holding a rehearsal store should discard it rather than expect it to load.

Two open offers now behave the way one always appeared to: each card answers its own proposal, and
both answers reach the same lock. The false *ignored* tell the client could manufacture is gone;
an *ignored* report now means what it says, which is what H1 will be read against.

## What this changes elsewhere

- **Code**: `GameLogic/TickLog.h` (`DigestEntry`), `GameLogic/Snapshot.cpp` (the archive visitor),
  `GameLogic/TickResolver.cpp` (five entry kinds and `OpenTradeLane`'s signature),
  `Lockstep/SnapshotView.cpp` (matching, and `OrdersOf`), `LockstepClient/MatchState.h`
  (`ProposalAnswer`), `LockstepClient/MainPage.cpp` (the tap handler, the answered card, the rail).
- **Tests**: `GameLogicTests/DiplomacyTests` gains `AProposalDigestEntryNamesItsProposal`;
  `LockstepTests/DigestViewTests` and `TapTests` gain the two-offer cases.
- **Design/**: `Design/UI/SCREENS.md` 01 gains the answered state of a proposal card.
  `Design/blueprint.md` §2 already describes the card correctly and needs no change.

## Open questions

**Whether an answered card should keep offering the other button** rather than only the answer it
carries. Today it draws both, with the chosen one marked, so changing an answer is one tap; the
alternative is a single `CHANGE` control. Phase 0 will show whether anyone changes an answer at
all.

**Whether the proposer should see their own offer as a card.** An offer this player made is not in
the PROPOSALS rail — it is a `Withdraw` row (ADR-039) — and nothing here changes that.
