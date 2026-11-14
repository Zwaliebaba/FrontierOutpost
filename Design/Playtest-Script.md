# Stage 1 playtest script

**Draft 1 · 12 November 2026 · For owner review**

With no testers available, plan M7 is closed by the owner alone, and this script's four sessions
are what the owner plays for it (plan 7.5). The script itself is kept for the playtest with other
players that gates 1 and 3 need before Stage 2 (plan M7, "What stays open").

This script runs the Stage 1 playtests: about eight invited testers who have not seen the game,
on machines the owner provides (design §9). It says what each session asks of a tester, what the
facilitator does and does not do, and how the results become the gate write-up (plan 7.6). Items
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

### Machines

- Each tester has their own operating-system account on the machine they use. Their passkey
  lives there (AGENTS.md §8), so a tester always signs in on the same machine and account.
- On the owner's own network only (AGENTS.md §8). A self-signed certificate, trusted on each
  machine, is enough; the code review's hardening for outside clients stays deferred.
- A Chromium-based browser (the only one tested), with the server's certificate trusted and the
  server reachable by name.
- Mouse and keyboard; sound as the owner prefers, the same for everyone.
- Screen recording on, if the testers agree, for reviewing stuck moments afterwards.

### Server

```sh
export FRONTIER_DATA=data/playtest.db                 # the server, wipe and report all use it
cargo run --release -p frontier-server -- wipe        # an empty world, once, before session 1
FRONTIER_SEEDS=9,10,20,16,8,3,5 \
FRONTIER_ORIGIN=https://<host name> \
cargo run --release -p frontier-server
```

- No `FRONTIER_GRANT`, `FRONTIER_TIERS`, `FRONTIER_NPCS` or `FRONTIER_SITE`: testers play the real
  economy, tiers and site.
- **Seeds by run number.** `FRONTIER_SEEDS` gives a tester's Nth run the Nth seed of the pool,
  wrapping after seven, so every tester flies the same site on the same run number, and runs 3
  and 10 share a seed. That feeds gate 1's and gate 3's same-seed measures; random seeds would
  leave them empty. The pool spans every hazard and threat type, and puts the rich pocket in
  each of the four pockets at least once:

  | Run | Seed | Hazard | Dominant threat | Rich pocket |
  | --- | --- | --- | --- | --- |
  | 1, 8 | 9 | Thermal flare | Balanced | Ore field |
  | 2, 9 | 10 | Debris drift | Balanced | Volatile veins |
  | 3, 10 | 20 | Ion fog | Raiders | Volatile veins |
  | 4 | 16 | Debris drift | Drones | Nest core |
  | 5 | 8 | Thermal flare | Raiders | Derelict |
  | 6 | 3 | Ion fog | Drones | Derelict |
  | 7 | 5 | Ion fog | Balanced | Ore field |

  A crew flies its host's seed, so a member's run number and seed can differ in paired sessions.
- Copy `data/playtest.db` somewhere safe after each day's sessions. Stage 1 data may be wiped,
  but losing it mid-playtest loses the measures.
- Check the build once on the playtest machines: sign up, launch, open the map (M), extract a
  unit, jump, dock. Then wipe again.

### Thresholds

Confirm the provisional thresholds of design §9 before session 1 and write them at the top of the
write-up, so the results are judged against figures chosen in advance.

## 4. The facilitator's rules

- **Do not teach.** The game's briefing, gauges, prompts, notices, map and key list are all the
  tester gets. If a tester is stuck for more than two minutes on something outside the game
  (sign-in, the browser, the machine), help. Inside the game, note it and wait.
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
- **Misread:** a gauge, the map, the waypoint or a notice taken for something else. Last build's
  examples were the heat bar read as the hold, and damage that could not be seen.
- **Defeat:** what killed them, what they said, and what they did next, unprompted.
- **Choice:** which pocket, fitting or tier, and their reason if they say one aloud.
- **The jump drive and the cruise drive:** found unaided? Used to skip travel, or ignored?
- **The system map (M, or the minimap):** found unaided? Did they choose a destination there, or
  hunt the scene for something to click? Without labels on the scene, could they find a pocket?
- **Prompts:** did a prompt lead to its key (extract, scan, vent), or go unread? Note any key a
  tester looked for and did not find.
- **The run status in the top bar:** did they see the alert rise or the drives jam, or learn it
  from what followed?
- **The scan's result:** once its notice had gone, did they look for it on the map?
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

## 8. Decisions and open items

Decided by the owner on 12 November 2026:

- **Seeds:** a pool of seven by run number (§3).
- **Network:** the owner's own network only (§3).
- **NPC cap:** wardens are placed first; the rest of a full instance's NPCs are dropped (design
  §10). It only binds for large crews at tier III.
- **Launch price:** goods queued for processing count toward it, like stock (design §10).

Changed on 13 November 2026, at the owner's request:

- **The flight HUD and the system map** (style guide §11): the jump destination is chosen on a
  system map, the scene carries no labels, the run's status is in the top bar, and prompts and
  notices replace the standing lines of text. Two things the earlier build showed all the time
  are now shown once or on request: a scan's result (a notice, then the map) and the key for each
  action (a prompt while it would act, and the footer's list). §6 watches for both. Check the
  build on the playtest machines again (§3) before session 1.

Open:

1. **Veteran and newcomer pairing.** Recruit two extra newcomers for the second week, or skip it
   in the first playtest (§2).

## 9. After the playtest

```sh
cargo run --release -p frontier-server -- report greybox/*.json > playtest-report.md
```

Run it with the same `FRONTIER_DATA` as the server. Write one section per gate:
the measure, the threshold confirmed in advance, the result, whether it passed, and the
facilitator's notes that explain it. A gate that fails is a finding, not a failure of the
playtest: record what would have to change, and what to measure next time.
