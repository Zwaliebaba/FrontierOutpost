# ADR-051 — A practice match is the same game on a faster clock

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, after a first-session report: *"as a starter I now give 2 instructions and I have to wait 6 hours before I can do the next step."*
**Supersedes:** —

---

## Context

The one-pager fixes the cadence and does not apologise for it: *"Four ticks a day at fixed UTC
times."* Six hours between locks is not a tuning value that drifted in — it is the mechanic the
whole design rests on. Orders are hidden until they lock, and the wait is what makes a commitment
blind; the opening image of the one-pager is somebody opening the app after work, and the pacing
section is careful to say that lane costs grow toward the frontier while *"the tick stays
constant."* A game that resolved every ten minutes would be a different game, and a worse one for
the person it is for.

It is also, for a new player, a wall. A first session is: look at a galaxy you have never seen,
issue two orders, and stop. Nothing resolves, so nothing is learned — not what a lane cost does to
an ETA, not what the digest looks like when it has something in it, not what happens when a fleet
arrives somewhere a rival also went. The next opportunity to learn any of it is six hours away, and
the one after that is twelve. Somebody who does not already know this genre has no reason to still
be there.

That is the one-pager's own fourth risk — *"Cold start. Six to eight strangers committing to three
weeks is a matchmaking problem before it is a design problem"* — reaching a player one step earlier
than the document expected it to. The problem is not getting six people into a lobby. It is that
the first hour of the game teaches nothing.

**Nothing in the engine was in the way.** `MatchRules::tickIntervalSeconds` has been a field rather
than a constant since it was written, because the test plan needed Phase 0 on an hourly tick;
`PhaseZeroRules()` has shipped a sixty-times-faster match since then, and `Neuron::TickSchedule` is
arithmetic over whatever interval it is handed (ADR-026). `GameLogic` never reads the interval at
all. A two-minute match was already reachable — by typing `--serve --phase0 --tick 3 --bots 6` at a
command line, which is an instrument for rehearsing a build and not something a new player will
ever find.

## Options considered

### A. Leave it, and fix the onboarding with words

A tutorial document, a better first digest, a stronger empty state. Costs nothing and changes
nothing: the player still cannot see a tick resolve, and reading about what a lane cost does is not
the same as watching a fleet arrive a tick later than the one you expected.

### B. A practice match: the same rules on a two-minute tick, against bots

A preset — `PracticeRules()` — reachable from the seats screen, that starts a match differing from
the real one in its clock and its opponents and in nothing else. Thirty ticks at two minutes is an
hour; the first five ticks are ten minutes, which is long enough to learn the loop and short enough
to do before dinner.

The cost is a second configuration of the game in front of players rather than only in the test
plan, and the honesty risk that goes with it: if a practice match played differently from a real
one, it would teach the wrong game and be worse than nothing.

### C. Shorten the real tick

Four ticks a day becomes eight, or twelve. It does not fix the first session — the beginner still
issues two orders and waits, just for less time — and it costs the mechanic the design is built on.
The wait is not friction to be reduced; it is the window inside which everybody commits blind.

### D. Lock early once every live player has submitted

Resolve as soon as everyone still playing has sent their orders. Attractive on the evening six
friends start a match together, and wrong in three ways that only show up later. It rewards
whoever answers fastest, which is the opposite of what an asynchronous game is for. It surprises
the player who stepped away for ten minutes and comes back to find two ticks have passed and their
standing orders resolved twice. And it breaks the property the schedule gets for free from ADR-026,
where the phase of day falls out of `startedAt` and six clients agree about when the tick is
without any of them running a timer.

There is a narrower version — lock early only during the first day, only with every seat's consent —
and it is still a second scheduling rule to reason about for a problem option B solves outright.

### E. An accelerated opening: the first day fast, ramping to six hours

The first several ticks run on a short interval and the schedule steps up to the designed one. It
fixes the complaint exactly as stated, and it is the most interesting of the rejected options.
What it costs is `TickSchedule`'s best property: locks are at `startedAt + n × intervalSeconds`, one
multiplication, and every client and the store agree about every lock in the match's history from
two numbers. A piecewise schedule is still pure arithmetic and still testable, but it is no longer
one that a player can hold in their head, and "when does this lock?" stops having an answer they can
work out. It also changes the real match rather than adding somewhere to practise, which makes it a
larger decision than the one this session was asked for.

## Decision

**B.** `Lockstep::PracticeRules()` is a match preset: six seats, a tick every two minutes, thirty
ticks — about an hour — and everything else exactly `MatchRules`' defaults. `PRACTICE MATCH` on the
seats screen hands every seat but the host's to a bot and enters immediately, waiting for nobody.

**The clock and the opponents are the only things it changes, and that is the whole decision.** A
practice match with more starting credits, a smaller galaxy or a gentler combat table would teach a
game the player is not about to play, and the lesson would not transfer. What a beginner needs is
not an easier game; it is the real one at a speed where the consequence of an order arrives while
they are still looking at the order.

**Three derived numbers do not survive being compressed, and they are scaled exactly as
`PhaseZeroRules` scales them** — `firstWeekTicks` stays a third of the match, `regionOpensAtTick`
five sevenths of the way through, `capitalGuardTicks` a seventh. They are fractions of a match, so
they stay fractions of one.

**`custodianAbsenceTicks` is the exception, and it is set to the match length so that it cannot
fire.** Absence is a rule about days away from a life, and there are no days in an hour. The
alternative — scaling it by the clock the way Phase 0 does, which would put eighteen hours at 540
ticks of a 30-tick match — reaches the same place by arithmetic that pretends to mean something.

**The command line still wins.** An explicit `--tick` after `PRACTICE MATCH` overrides the preset,
because a flag that was typed on purpose is a statement of intent and a button is a default.

## Consequences

**A beginner can learn the loop in ten minutes**, in the client, without a command line, without
five other people, and without being taught a different game. Five ticks resolve while they are
still sitting there: a fleet arrives, a digest fills, income lands, a bot does something they did
not expect.

**There are now two clocks a player can meet**, and the screen has to say which one they are
starting. The practice box says the two numbers outright — two minutes against six hours — rather
than describing a mode.

**The bots are the ones the scripted match already uses** (ADR-037), so a practice opponent is a
`BotPolicy` with the same view of the board a human in that seat would have, and nothing had to be
written to make the match playable alone.

**A practice match is a real match to the server.** It is stored, logged and resolved like any
other, so the store directory accumulates hour-long matches nobody will resume. That is the same
behaviour `--phase0` already had and it is not made worse here, but it is now reachable by somebody
who does not know the files exist.

**It does not answer the cold start** the one-pager names. Six strangers committing to three weeks
is still a matchmaking problem; this makes the first of those three weeks survivable for somebody
who has never played, which is a different and smaller claim.

**It exposes a hard-coded day counter.** `SnapshotView.cpp` computes `match.day` as `1 + tick / 4`
and sets `match.totalDays` to 21, both of which are the production match written into the view
layer. A practice match therefore reports `D1/21` in the top bar and will say `D8/21` at its end.
The snapshot carries neither `matchLengthTicks` nor `tickIntervalSeconds`, so fixing it is a change
to the wire record (ADR-049) rather than to the two lines that are wrong. **It is a defect, it is
not introduced here, and this ADR is where it was found.**

## What this changes elsewhere

- **Design/:** `Design/GETTING-STARTED.md` is written against this decision and tells a new player
  to start here. The one-pager is unchanged — the real match's cadence is untouched.
- **Code:** `GameLogic/MatchRules.h` gains `PracticeRules()`. `Lockstep/SeatsPage.{h,cpp}` gains
  `Entry` and the practice offer; `TakeEnterRequest` returns which match was asked for.
  `Lockstep/Lockstep.cpp` picks the preset. Done, built, and run.
- **AGENTS.md:** nothing.

## Open questions

**Whether a practice match should be stored at all.** It is an hour long and nobody resumes one.
R13's exception for a match store is written per-server rather than per-match, and suppressing the
store for practice would be a new exception rather than a narrowing of that one.

**What happens at the end of one.** A practice match finishes like any other — final standings, and
the screen ADR-038 puts up. Whether finishing should offer the obvious next thing, a real lobby, is
not settled here.

**Whether two minutes is right.** It is authored, not measured: five ticks in ten minutes is the
target, and nobody has yet watched a first-time player against that clock. It is one number in one
preset and Phase 0 is the place to change it.

**Option E is not closed, only deferred.** An accelerated opening addresses something a practice
match does not — the first day of the *real* match, with the other five people in it — and if
playtests show the drop-off is there rather than before the lobby, it is the next thing to write.
