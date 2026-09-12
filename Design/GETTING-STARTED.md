# Getting started — LockStep: Universe

**What this is.** How to play your first match, for somebody who has never played this game or
anything like it. It describes the client as it is built on 2026-09-12, not what is planned; where
something is a stub or is not wired up yet, it says so. The design it is written against is
[`space-4x-one-pager-v10.md`](space-4x-one-pager-v10.md), and the numbers come from
[`GameLogic/MatchRules.h`](../GameLogic/MatchRules.h), which is where every one of them lives.

---

## 1. Start here: a practice match

**Do not make your first match a real one.** A real match resolves four times a day at fixed times,
six hours apart. Your first session would be: look at a galaxy you have never seen, issue two
orders, and wait until tomorrow to find out what either of them did. You would learn nothing, and
there is a version of this that takes ten minutes instead.

Run `Lockstep.exe`. You get the join screen: a server, already filled in with your own machine, and
a token, already filled in with your own seat. Press **JOIN**.

That puts you on the seats screen, which is where a host arranges a match. Ignore all of it and
press **PRACTICE MATCH**. Every other seat becomes a bot, the match starts immediately, and the
tick is two minutes instead of six hours — thirty of them, about an hour, though you can stop
whenever you like.

**A practice match is the real game on a faster clock** (ADR-051). Same rules, same galaxy
generator, same combat, same everything except how long you wait and who you are playing. What you
learn in it is true of a real match, which is the entire reason it is built this way.

Give it ten minutes. That is five ticks, and five ticks is enough to see a fleet arrive, a digest
fill up, income land, and a bot do something you did not expect.

---

## 2. What you are looking at

The screen is three columns and a bar.

**The top bar** is the state of the match in one line: which match, which day of how many, how many
players and systems, then the countdown to the next lock, your score and placement, and who is
leading. **The countdown is the clock everything else runs on.** When it hits zero, everything
everybody ordered happens at once.

**The digest, on the left, is the primary screen** — not the map. It is what changed since you last
looked, sorted by how much it matters to you: a system lost above a proposal received above income.
Every event carries its own buttons. This is where you play from. On tick zero it says *nothing has
happened yet*, because nothing has.

**The map, in the middle,** is the galaxy: systems as dots, lanes as lines between them, and a
number on each lane. **That number is the lane's cost in ticks** — how long a fleet takes to cross
it. It is authored per lane when the galaxy is generated, not derived from how far apart the dots
look. Lanes inside your starting cluster cost one tick; lanes out toward the frontier cost two to
four. You can drag the map to turn it and pinch or scroll to zoom.

**The locks rail, on the right,** is a read-only receipt of what goes in at the next lock: your
fleets, your builds, your signals, your proposals. It has no controls — tapping a row jumps to the
event that owns it. It is there so that before the tick you can read what you have actually
committed to, which is rarely quite what you thought.

---

## 3. Your first three ticks

You start with a capital, two satellite systems joined to it by one-tick lanes, ten ships in one
fleet parked at the capital, and twenty credits.

**Tick one: move the fleet.** In the digest, press **MOVE FLT 1**. A sheet comes up from the bottom
listing every system that fleet can reach along a lane, with who holds it and how long it takes.
Pick something unclaimed one tick away. The locks rail now says your fleet is going there.

That is the whole core verb of this game, and it is worth saying plainly: **an order is a bet placed
now and resolved later.** Nobody else can see it until it locks. You cannot see theirs either.

**Tick one, also: spend the credits.** Press **BUILD** on a system you hold. Twenty credits is
exactly one shipyard, or a mining station with five left over. A shipyard adds two ships a tick to
the fleet at that system; a mining station adds four credits a tick to the system. Neither is wrong.
Build something — an empty first tick is a wasted one.

**Wait for the lock.** Two minutes in practice. You can change your mind about anything up to the
moment it hits zero; orders are editable right up to the lock and hidden until it.

**Tick two: read the digest.** It now has something in it. Your fleet arrived. Your income landed.
Something happened somewhere you can see. **Read it top to bottom** — it is sorted by consequence,
so the first item is the one that matters most.

**Tick three: claim it.** A system with your fleet on it and no hostile fleet contesting it becomes
yours at the end of the tick. Then move on. The galaxy is fully claimed by about day five in a real
match, and the players who got there first are the ones who never left a fleet sitting still.

---

## 4. The five rules that decide everything

**Movement happens before combat, so leaving beats arriving.** If you order a fleet out of a system
the same tick a hostile arrives, you escape. This is deliberate: it means a fleet that refuses to
fight cannot be hunted down, and it means the defender's real decision is always *do I stay*. The
cost of dodging is that you cede the system for a tick and start a siege you have to come back into.

**Taking an owned system takes two ticks, not one.** You have to be there, uncontested, at the end
of two consecutive ticks — siege, then capture. So losing a system is always something you saw
coming and had a tick to answer. The same is true in reverse: what you are taking, they can see you
taking.

**Capitals cannot be attacked for the first twelve ticks.** There is a visible countdown. Everything
else is takeable from tick one.

**You can only see one lane out from what you hold.** Your systems and their immediate neighbours.
Everything else on the map is what you were told, not what is there — and it can be out of date.

**Absence turns you into a custodian.** Three ticks without logging in — eighteen hours at the real
cadence — and your empire stops expanding and starts defending, its garrisons weakening each tick
you stay away. It is reversible: log in and you are back. It is also public, flagged on everybody's
map, which makes your territory a race among every neighbour who can reach it. In a practice match
it cannot happen; there are no days in an hour.

---

## 5. Talking to people

There is no chat, on purpose. The design's own words are that *strangers click but don't write*.

What there is instead is **proposals**, which are buttons: open a trade lane, share scouting, hold
for some ticks. A proposal is an order — it locks with the rest of yours, arrives in the other
player's next digest, and stays open for four ticks so that anybody playing twice a day sees it at
least once. You can withdraw it while it is open. If it runs its full four ticks unanswered, you are
told it was ignored, which is itself information.

**The trade lane is the one mechanic that needs somebody else to agree.** A lane between two
neighbours who both want it pays each of them six a tick; a lane between two systems you hold
yourself pays one. That gap is the whole incentive to talk to the empire next door instead of
expanding into them. Either side can cancel at any tick, lanes are public, and cancelling one is a
tell everybody can read.

After about day five the map runs out. From then on, lanes are the only way your income grows.

---

## 6. Winning, and not winning

The match ends on a date fixed when it starts — twenty-one days, eighty-four ticks — and placement
is by score on that date. **Score is what you hold now**, recomputed every tick: ten a system,
twenty-five more for a capital. It is not a running total, so a player who loses half their empire
drops immediately, which is what keeps the leader attackable.

Everybody's score is public. That is the anti-snowball: there is no rubber-banding, just the fact
that the leader is visible and gangable.

One player holding sixty per cent of all the score on the board for four consecutive ticks ends the
match early. Four consecutive ticks, not one, so that leading is not the same as winning.

**Most players lose.** In a match of six, five do. Placement is what is being played for, and fourth
is worth playing for — a match you spend climbing from sixth to third is a match you played well.

---

## 7. Things the client does not do yet

Stated so you do not go looking for them:

- **Replay** opens and lists the six resolution phases, and does not step through them. It is a
  stub and says so.
- **Missed digests** are one tick deep. If three ticks pass while you are away you get the latest,
  not all three.
- **Exile** — the mode you enter when your capital falls — is designed and not built. So is the
  Fallow, the sealed region the map draws and counts down to.
- **There is no mobile version.** The client is Windows and Direct3D 12, at a fixed 1280×720.

---

## 8. Running it other ways

The default — double-click `Lockstep.exe` — starts a server and plays on it. Everything below is
optional.

| | |
|---|---|
| `--join <host[:port]>` | Join somebody else's match instead of hosting. |
| `--token <token>` | The seat you were given. A token is not a password: whoever types it plays that empire, so send it to one person (ADR-029). |
| `--serve [port]` | A dedicated server with no window. |
| `--tick <seconds>` | Override the tick interval. Wins over anything the seats screen chose. |
| `--phase0` | The test plan's setup: six players, an hourly tick, forty-eight hours. |
| `--bots <n>` | Fill the last *n* seats of a `--serve` match with bots. |

**To play a real match with people**, host it, then send each of them one token off the seats
screen — each token names one seat, and the empire it plays. They run the client, put your address
and their token into the join screen, and connect. When everybody is there, press **ENTER MATCH**.
If somebody does not show, set their seat to **BOT TAKES OVER** and enter without them, or press
**FILL WAITING WITH BOTS**.
