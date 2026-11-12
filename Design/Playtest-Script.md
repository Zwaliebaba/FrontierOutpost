# Stage 1 playtest script

**Draft 1 · 12 November 2026 · For owner review (plan item 7.4)**

This script runs the Stage 1 playtests: about eight invited testers who have not seen the game,
on machines the owner provides (design §9). It says what each session asks of a tester, what the
facilitator does and does not do, and how the results become the gate write-up (plan 7.5). Items
marked **Open:** wait on the owner.

## 1. What the sessions must produce

The gate report (`frontier-server report`) computes most measures from the saves. The sessions
only have to create the conditions for them, and not spoil them.

| Gate (design §9) | Needs from the sessions | Measured by |
| --- | --- | --- |
| 1. Outpost changes decisions | Several sessions per tester, so facilities are bought and layouts change; testers on shared seeds; the before-and-after image question | Report; facilitator's notes |
| 2. Targeting stays readable | A greybox block per tester, in normal, grey and one colour-deficient vision mode | Greybox JSON files, read by the report |
| 3. Players repeat after loss | Defeats happen and nobody prompts what follows; testers can come back the next day unasked; about ten runs per tester | Report |
| 4. Travel and recovery | Ordinary play; nothing extra | Report |
| Crew evidence (not a gate) | Paired sessions of a veteran and a first-session newcomer, and the veteran on two clients | Report; facilitator's notes |

## 2. Testers and schedule

- **Eight testers** who have not seen the game, any build, screenshot or this repository. Mixed
  experience with games; at least two who rarely play space or strategy games.
- **Four sessions per tester**, each 60 to 75 minutes, over seven to ten days, with at least one
  night between sessions. That is about five hours each: the design's content horizon of roughly
  ten runs, which gate 3's run 3 against run 10 needs.
- **Unprompted return (gate 3).** After the first session, testers are told the windows in which
  a machine is free, and that coming is up to them. Nobody reminds or chases them. Whether and
  when they come back is the measure.
- **Veteran and newcomer (crew evidence).** In the second week, two testers who have played at
  least three sessions each pair with one newcomer recruited for that day. **Open:** whether the
  owner recruits two extra newcomers for this, or skips the pairing in the first playtest. The
  owner cannot stand in as the veteran: they know the game too well for the comparison to mean
  anything.

## 3. Before the first session

### Machines (plan 7.3)

- Each tester has their own operating-system account on the machine they use. Their passkey
  lives there (AGENTS.md §8), so a tester always signs in on the same machine and account.
- A Chromium-based browser (the only one tested), with the server's certificate trusted and the
  server reachable by name.
- Mouse and keyboard; sound as the owner prefers, the same for everyone.
- Screen recording on, if the testers agree, for reviewing stuck moments afterwards.

### Server

```sh
export FRONTIER_DATA=data/playtest.db                 # the server, wipe and report all use it
cargo run --release -p frontier-server -- wipe        # an empty world, once, before session 1
FRONTIER_ORIGIN=https://<host name> \
cargo run --release -p frontier-server
```

- No `FRONTIER_GRANT`, `FRONTIER_TIERS`, `FRONTIER_NPCS` or `FRONTIER_SITE`: testers play the real
  economy, tiers and site.
- **Open: the seed policy.** Without `FRONTIER_SEED` every contract has a random seed, so no two
  testers share one and gate 1's and gate 3's same-seed measures have no data. See §8.
- Copy `data/playtest.db` somewhere safe after each day's sessions. Stage 1 data may be wiped,
  but losing it mid-playtest loses the measures.
- Check the build once on the playtest machines: sign up, launch, extract a unit, jump, dock.
  Then wipe again.

### Thresholds

Confirm the provisional thresholds of design §9 before session 1 and write them at the top of the
write-up, so the results are judged against figures chosen in advance.

## 4. The facilitator's rules

- **Do not teach.** The game's briefing, gauges and key hints are all the tester gets. If a tester
  is stuck for more than two minutes on something outside the game (sign-in, the browser, the
  machine), help. Inside the game, note it and wait.
- **Never prompt a relaunch.** After a defeat, say nothing about what to do next. Whether the
  tester launches again is gate 3.
- **No thinking aloud during play.** It slows people down, which skews gate 4's timings. Ask
  questions afterwards instead.
- **Note, do not fix.** Bugs and confusions go on the observation sheet (§6). The build does not
  change during the playtest.
- **The same words for everyone.** Read the opening and the questions as written.

## 5. Sessions

### Session 1: first contact (60 minutes)

1. Opening, read aloud: "This is an early build of a space game. Play it however you like. I
   won't explain anything, because I want to see what's clear and what isn't. Stop whenever you
   want, and at the end I'll ask a few questions."
2. The tester signs up with a callsign of their choice and plays. The facilitator watches and
   notes the moments in §6, especially the first two minutes after launch (gate 4's first
   decision).
3. Stop at 60 minutes, or earlier if the tester wants to. Let a run in progress finish if it is
   within a few minutes.
4. Questions (§7), then tell them the windows for coming back (§2).

### Session 2: greybox and play (75 minutes)

1. **Greybox block, 15 minutes (gate 2).** Open `/greybox.html` on the same server. Three runs of
   eight trials, all in the station scene at the default settings:
   - `?scene=station&vision=normal`
   - `?scene=station&vision=grey`
   - `?scene=station&vision=deutan` (alternate `protan` between testers)

   Explain the three trial kinds once, from the panel. Space starts each run; download the JSON
   after each, and name the files `<callsign>-<vision>.json`.
2. Free play for the rest of the session. Questions afterwards.

### Session 3: play and the outpost (75 minutes)

1. Free play.
2. **Before-and-after images (gate 1).** At the end, show two overview screenshots of the
   tester's own outpost: one from their first session and one from now. The facilitator captures
   these at the end of every session. Ask: "What changed between these two? What does that let
   you do that you couldn't before?" Note the answer word for word.
3. Questions.

### Session 4: play, and the pairing where it applies (75 minutes)

- Testers not in a pairing play freely.
- **Pairing (crew evidence).** The veteran and the newcomer crew up by code, with the veteran as
  host, and play together for 45 minutes. Then the veteran plays two clients on two machines,
  alone, for 20 minutes. Afterwards ask each separately: "Would you play another run together?"
- Final questions, including the closing ones in §7.

## 6. Observation sheet

One sheet per tester per session. Times are from the session clock.

| Time | What happened | Category |
| --- | --- | --- |
| | | Stuck · Misread · Defeat · Choice · Quote · Bug |

Look out for:

- **Stuck:** more than 30 seconds without knowing what to do. Where were they, and what did they
  try?
- **Misread:** a gauge, marker or notice taken for something else. Last build's examples were the
  heat bar read as the hold, and damage that could not be seen.
- **Defeat:** what killed them, what they said, and what they did next, unprompted.
- **Choice:** which pocket, fitting or tier, and their reason if they say one aloud.
- **The jump drive and the cruise drive:** found unaided? Used to skip travel, or ignored?
- **Quote:** anything said about wanting to come back, or not.

## 7. Questions after each session

Ask these in order, and write the answers word for word.

1. What were you trying to do this session?
2. What was the most confusing moment?
3. Was there a moment you wanted to stop? When?
4. (After a defeat) What would you do differently?
5. (Sessions 2 to 4) What has your outpost become, and why that?

After the last session, also ask:

6. Would you play this again if it were available? What would make you?
7. If you described this game to a friend, what would you say it is?

## 8. Open items for the owner

1. **Seed policy.** Same-seed measures need testers on shared seeds. Options: one fixed seed for
   the whole playtest; a small pool of seeds assigned by run number, so every tester's run 3 shares
   a seed (with seven seeds, run 3 and run 10 even share the same one); or random seeds, giving up
   those measures.
2. **Veteran and newcomer pairing.** Recruit two extra newcomers for the second week, or skip it
   in the first playtest (§2).
3. **The review's open questions** (2026-11-11 code review §8): what happens when the NPC cap
   binds, whether queued processing counts toward the launch price, and which gate admits clients
   the owner does not control. The last one decides whether the review's §4 hardening is due
   before testers connect over a network the owner does not fully control.

## 9. After the playtest

```sh
cargo run --release -p frontier-server -- report greybox/*.json > playtest-report.md
```

Run it with the same `FRONTIER_DATA` as the server. Write one section per gate:
the measure, the threshold confirmed in advance, the result, whether it passed, and the
facilitator's notes that explain it. A gate that fails is a finding, not a failure of the
playtest: record what would have to change, and what to measure next time.
