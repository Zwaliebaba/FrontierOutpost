# Review of the Frontier Outpost code: performance, practice and scale

**11 November 2026 · Multi-agent review · Code at commit `60c7306`**

Seven independent reviewers each read the code through one lens: simulation hot path, server concurrency, netcode and protocol, persistence and economy, client performance, code quality, and scale-out and operability. Each had to cite `path:line` and back performance claims with a measurement or a complexity argument. Six challengers then tried to refute the critical and high findings and every "fix now" finding, by re-reading the code and re-running or rebuilding the measurements. This report records what survived. Nothing in the repository was changed by the review.

The question asked was what stops this code growing from Stage 1 towards hundreds of users. Every finding was judged against four load scenarios:

| Scenario | Load | Status |
|---|---|---|
| **S1** | One instance: up to 4 pilots, 24 NPCs, 64 projectiles, 20 Hz | Built today |
| **S-many** | 300 concurrent users on one process, in about 100 instances | Reachable with today's design |
| **S-site** | 40 player ships in one simulation (design §5) | Stage 2, not to be built now |
| **S-region** | 150 players in one shared region (design §8) | Stage 2+, not to be built now |

Findings keep their reviewer's identifier (`SIM`, `SRV`, `NET`, `DB`, `CLI`, `QUAL`, `OPS`) so they can be traced through the appendix.

## 1. Verdict

**The code is sound for Stage 1, with four correctness defects to fix before the next playtest.** The architecture holds up: one task per instance, an I/O-free simulation, a guarded binary protocol, strict lints passing clean, and tests on every recent behaviour change. The defects are in rules at the edges, not in structure: unchecked arithmetic on client-supplied trade amounts (2.1), crew runs that end or pay wrongly when a pilot drops or the server restarts (2.3), a follower charged for a launch that is then refused (2.4), and a ship that keeps flying and firing when its tab is hidden (2.5).

**Capacity is not what breaks first at S-many; exposure and operability are.** A load bot drove the unmodified release server with 300 pilots in 100 instances at 31–36% of one core and 37 MB, every pilot receiving 20 snapshots a second; 300 pilots in real combat cost the same, and 600 pilots in full crews cost 51%. The single SQLite connection has 40 to 65 times the write capacity that load needs. What fails first is everything that assumes invited testers: one crafted message mints currency, one connection can grow server memory without bound, and nothing limits sessions, streams or message rates. Next comes operation: the server logs nothing when a tick overruns, a stop drops runs, and a write lock taken on the database from outside closes players' sessions. On a home connection the binding resource is egress, at 12 to 33 Mbit/s for 300 pilots.

**For S-site and S-region the constraint is the snapshot, not the simulation.** A tick with 400 ships in one world costs about 1.5% of the 50 ms budget, so no spatial index or data-layout work is justified by any named scenario. The single-datagram full-state snapshot stops at 54 entities, which is 40 ships plus 14 others. The other decisions that bind later are the pilot-against-everything hostility rule, a 16-bit ship identifier, positions limited to ±1,024 m, two draw calls per ship on the client, and registries held in process memory. None needs building now, and none is expensive to change later, because client and server are version-locked and served together.

The review started from five suspected limits. The evidence removed three of them:

| Suspected limit | Evidence | Outcome |
|---|---|---|
| Per-tick simulation cost grows quadratically | True, and measured at 0.04% of the tick budget at S1 and 1.5% at 400 ships | Not a limit through S-region |
| One SQLite connection behind a mutex | 6–10 write transactions a second needed at S-many; 370–670 measured on a slow disk, 4,000–8,000 on a fast one | Not a limit at S-many |
| One task per instance on a shared runtime | 300 pilots in 100 instances over real WebTransport: 20.0 snapshots a second each, on 31–36% of one core | Not a limit at S-many |
| Unbounded channels | One connection grew the server by 1–5 MB a second at a 10–16 Mbit/s uplink | A limit once strangers can connect |
| Full-state snapshot in one datagram | 54 entities maximum; S1 peaks at 29 | A limit at S-site |

## 2. Fix now

Ranked. Each item was confirmed by a challenger unless marked otherwise. Effort: S is a day or less, M a week or less.

### 2.1 Client-supplied amounts are multiplied unchecked

**High at S1, critical once the server is reachable by anyone uninvited · QUAL-1, DB-1, SRV-1, OPS-1 · found independently by four reviewers, reproduced over the real wire by the challenger**

`crates/server/src/home.rs:240` computes `market::price(good).sells_at * amount` before `afford` runs, and `amount` is the raw `u32` from `ClientMessage::Buy`. Nothing clamps it between the protocol and `home::act`. The workspace has no `[profile]` section, so a release build wraps and a dev build panics.

- **Release** (what the container ships): `Buy ore 2^28` costs nothing; a following `Sell` leaves a new 100-credit account with 1,000,000,100 credits. `Process alloy 2^31` queues batches for no ore, and `Unqueue` returns ore the account never held. The ledger records all of it as legitimate trade, so the conservation test cannot see it.
- **Dev** (what `cargo run`, the README and the Playwright configuration start): the same frame panics inside the store closure with the connection mutex held. Every later store call from every session fails until restart (2.2).
- **Other players are affected.** An account near `u32::MAX` makes `Transfer::apply` return `Overflow`, so `pay_pot`'s all-or-nothing batch pays nobody in that crew (by reading).

Unchecked sites: `home.rs:226`, `:240`, `:258`, `:281`, `:348-350`; `crates/sim/src/market.rs:120`; `crates/sim/src/crew.rs:25` (`valid_split(&[u32::MAX, 101], 2)` is true in release). The game's own interface cannot send these amounts, so the trigger is a hand-crafted frame from a signed-in client.

**Fix.** Use `checked_mul` and `checked_add`, refusing on overflow, at every site; cap `amount` and `batches` at the `home_action` boundary; reject a share over 100 before summing; add tests with `u32::MAX`-range amounts for Buy, Sell, Process, Unqueue and SetSplit. Then set `overflow-checks = true` in `[profile.release]`, whose measured cost is nil. **Do not enable it before 2.2 is fixed**: on its own it converts the release exploit into the store-wide outage. Effort S.

### 2.2 A panic under the store lock disables persistence until restart

**Medium · QUAL-2, DB-3, SRV-9, OPS-2 · confirmed by running**

`crates/server/src/store.rs:266` maps a poisoned mutex to a permanent `StoreError::Poisoned`, while `runs.rs:41` and `crews.rs:122` recover with `into_inner`. `Store::update` runs caller code under the guard, so any panic there stops every session and every instance from saving, and the process stays up looking healthy: HTTP keeps answering 200, and banking failures are only logged at `warn`. The only reachable trigger found is 2.1 in a dev build, which is why this is medium and not high.

**Fix.** Recover the guard with `into_inner` and log the panic at `error`. The challenger verified this is safe: a rusqlite `Transaction` rolls back on unwind, leaving the connection in autocommit with nothing half-written. Give `store.rs:281`'s `JoinError` its own variant. The instance task has the same gap (a panic in `Run::step` ends the instance silently with nothing banked; by reading), and deserves the same treatment. Effort S.

### 2.3 Crew runs end or pay wrongly when a pilot drops or the server restarts

**Medium at S1, high at S-many · SRV-5, DB-2, SRV-6, OPS-4 · three linked defects, each reproduced**

1. **The last pilot flying gets no grace.** Once any crewmate has docked or been destroyed, a dropped connection for the remaining pilot ends the run one tick later: abandoned, cargo lost, reconnect refused. `check_end` (`crates/sim/src/run.rs:1683-1703`) ends the run when someone is over and everyone else is disconnected. A solo pilot gets five minutes. No restart is involved, so this is the variant most likely to be met in play. The test at `run.rs:2674` pins only the opposite order.
2. **A restored crew run with a finished member ends on its first tick.** `Run::restore` marks every unfinished pilot away (`run.rs:1032`), so the same rule fires at once. Against the real binary: `tick=2400 Restored`, then `Abandoned`, `PotSettled`, `Ended` within 2 ms, and the checkpoint row deleted before anyone could reconnect. Design §6 promises each ship is restored at its last checkpoint.
3. **Settlement is not atomic with the checkpoint.** A dock banks cargo in its own transaction; the checkpoint is rewritten only each minute and on pocket entry, and `finish()` never requests one. Against the real binary: a crew host mined one ore and docked, the server was killed 0.3 s later and restarted, the host was restored flying with the ore and docked again. Result: two ore, two ledger rows for one run. The window is up to 60 s in a crew run and a few milliseconds solo.

Two related gaps: no checkpoint is written at launch, and none at shutdown. A server stopped 20 s into nine runs left no checkpoint rows, with 360 credits of launch supplies already debited. Shutdown drains only HTTP; the process exits in 5–16 ms, and clients notice 8–10 s later through the idle timeout. The page then reloads onto the browser's own error page, since the same process serves it; a full Chromium retried by itself and was back 17–47 s after the server returned.

**Fix, in this order.** Decide the end rule (question 8.1), then fix defects 1 and 2 together with a crew restore test. Only then set `checkpoint_due` in `finish()`, a one-line change that shrinks the double-bank window from 60 s to one commit; done earlier, it makes defect 2 fire immediately. Add a checkpoint at launch and on the shutdown signal, and close sessions with a reason the client can act on. Full atomicity (settlement, pot and checkpoint delete in one transaction) is larger than the reviewer estimated, because settlement is per event and the checkpoint per tick. Effort M in total.

### 2.4 A follower is charged launch supplies before the join succeeds

**Medium · QUAL-3, DB-5, SRV-11 · confirmed by running**

`crates/server/src/transport.rs:803` pays supplies, then `:809` asks the instance to join. `open_launch` checks only the wall clock; `Run::join` also refuses when seats are taken, counting finished pilots. A follower who flies out, docks and presses "Follow the host" again inside the window is charged and refused. The button stays enabled, and `supplies_due` charges whatever the pilot has, so repeated presses took a pilot from 100 credits to 60, 20, then 0.

**Fix.** Join first and charge on success, or refund through the ledger on refusal. Stop offering a launch to a member who has already flown in it. Do the charge in one `Store::update` closure (DB-11). Effort S.

### 2.5 A hidden tab keeps flying and firing

**Medium · NET-2 · confirmed in headed Chrome**

`InputQueue::take_for_tick` repeats the last input with no age limit (`crates/server/src/input_queue.rs:53-62`). The client sends inputs only from `requestAnimationFrame`, and `blur` clears held keys without sending anything. Switching tabs with thrust and fire held stops the frames within 23 ms and leaves the last input applied for as long as keep-alive pings hold the session.

**Fix.** Hand out neutral controls after about ten ticks without a fresh input, and send one neutral input on `visibilitychange` and `blur`. This does not conflict with the existing repeat test, which repeats three times. Effort S.

### 2.6 Server-side input delay ratchets up and stays up

**Medium on an impaired link · NET-3 · confirmed; trigger wider than reported**

The input backlog holds up to three inputs and nothing ever consumes two in a tick. One lost datagram is enough: the server repeats once, then the redundant copies queue as fresh input. On the plan's own impairment settings (jitter 0–20 ms, 2% loss) the delay sits 100 ms higher within about 30 seconds. It affects unpredicted actions and what crewmates see, not the pilot's own movement.

**Fix.** Drain using the minimum backlog over a window, and test under `FRONTIER_IMPAIR_JITTER_MS` and `_LOSS_PERCENT`. The reviewer's simpler rule (drop one input after a second at backlog two) churns on a jittery link: 176 repeat-and-drop pairs in 20 minutes. Effort S to M.

### 2.7 The SIGTERM handler is registered after the server says it is open

**Medium · QUAL-4 · confirmed; wider than "flaky"**

`main.rs:85` logs "open"; the handler is created only when `shutdown_signal()` is first polled inside `axum::serve`. Signalling the instant that line appears killed the debug server 149 times in 150 and the release server 94 in 100. The blocking test `stops_cleanly_on_sigterm` passes only because spawning `kill` usually takes longer than the window; it failed 17 of 150 runs under load. A `docker stop` soon after start hits the same gap.

**Fix.** Create the `Signal` stream in `main` before the "open" line and pass it to `shutdown_signal`. Effort S.

### 2.8 Logging blocks the runtime's worker threads

**Medium · SRV-4 · confirmed**

`tracing_subscriber::fmt()` writes to stdout on the calling thread. With stdout to a pipe nobody reads, 40 connecting sessions were enough to stop every worker: no ticks, no HTTP. It heals when the reader resumes. The ledger's `info!` at `store.rs:652` is worse: it runs with the database mutex held, so one blocked line stops every store call. It is also emitted before the commit, so a rolled-back batch still prints "transfer". A plain terminal does not stall; a Windows console with text selected would (not tested here).

**Fix.** A bounded channel to a writer thread behind `with_writer`, dropping on full; about 30 lines and no new dependency (`tracing-appender` is not in the lockfile). Move the ledger line to after the commit. Effort S.

### 2.9 Smaller defects

Each is S. The client items were measured by the reviewer and not challenged.

| Finding | Location | Fix |
|---|---|---|
| A hostile lock inside the site does not count as combat for the disconnect timer: 10 s of exposure instead of 30. Confirmed on 58 of 58 seeds. Exposure is fixed at disconnect, so this is the only thing between a pilot and a free escape. (SIM-2) | `crates/sim/src/run.rs:1100-1107`, `:749-757` | Keep `hunted` as its own flag; test with the hostile beyond weapon range, inside and outside the site. The existing test passes through the `Fired` branch and never exercises the lock |
| Snapshot events queue without bound while the tab is hidden, then replay in one frame and grow the bolt pool for good: 80 meshes and 320 GL buffers after 20 s. (CLI-2) | `client/src/main.ts:342`, `:782`; `client/src/game/scene.ts:174` | Drop events older than a second; cap the pool at the projectile cap |
| When the NPC cap binds, the wardens that are placed cover half the core, because the ring is spaced by the planned count. The core also takes a cap slot, though `run.rs:1062` says it is not an NPC. (from the SIM-1 challenge) | `crates/sim/src/run.rs:1396-1398`, `:1347` | Space the ring by the placed count; exclude the core from `npc_count` |
| The home screen draws the station as 379 meshes every frame: 383 draw calls against 67–80 in combat. (CLI-4) | `client/src/outpost/models.ts:35-53` | Merge per material at import: 12 draws |
| The lock bracket's transparent double-sided material makes three.js rebuild program parameters 16 times a frame: 40% of per-frame allocation in combat. (CLI-5) | `client/src/view/readability.ts:139-145` | `forceSinglePass: true` |
| The HUD rewrites the DOM and forces a layout every frame. (CLI-6) | `client/src/main.ts:718-728`, `:812-856` | Write only when the string changed; keep the `data-*` the e2e tests read |
| Stale snapshots queue for a stalled client; the comment at `expedition.rs:59` says they are dropped. Worst staleness measured 7 s. (NET-4, SRV-12) | `crates/server/src/transport.rs:149-151` | `datagram_send_buffer_size` of 4 KiB or more; fix the comment |
| The interpolation clock offset only moves one way, so skipped server ticks or clock drift cost the jitter buffer for the rest of the run. Visible as heading judder, not stepping. (NET-1, CLI-1) | `client/src/net/interpolation.ts:38-48` | A slewed windowed minimum, with a skipped-tick test |
| One malformed checkpoint row stops the server starting. (SRV-13) | `crates/server/src/main.rs:95` | Log and skip the row |
| The sign-in refusal echoes an unvalidated callsign of up to 64 KiB. | `crates/server/src/auth.rs:50` | Check the length first |
| Stale comments, and one subtraction on a checkpoint-derived value that should saturate. (QUAL-11) | `expedition.rs:6-7`, `:536`; `world.rs:23`; `run.rs:367` | Correct them |

### 2.10 Tooling to add now

These are tools, not Stage 2 systems. The challenger cut each to its smallest useful part; all are low at S1.

- **Run e2e against the release build.** No job compiles the server in release, and e2e runs against a debug server; that is what hid 2.1. It is a one-flag change. A `caps` tripwire is optional, and sound only as a release step: the debug median is 46 µs, too close to a 100 µs bound for shared runners. (OPS-9)
- **Log an overrun.** There is no metric, tick-duration measurement or overrun log anywhere in `crates/server`, and `MissedTickBehavior::Skip` hides overruns. About ten lines cover it: a `warn!` when a loop turn exceeds one tick, and when a store call exceeds some tens of milliseconds. Holding the database locked for 0.5 s from outside stalled eight instances for 110–530 ms and the server logged nothing. Histograms and a `/metrics` route belong with the load bot at the scale gate. At S1 the client's HUD already shows round trip and prediction correction. (OPS-3)
- **A load bot.** The reviewer assembled one in about 300 lines from `tests/common` and the `Pilot::fly` helper, with no server change, and the challenger confirmed it does real work: passkey registration, crews by invite code, inputs at 20 Hz, decoded snapshots. The helpers live under `tests/`, so nothing else can import them; move them into a small crate the integration tests also use. (OPS-10; not challenged as a finding)
- **One README line on backups.** Recent data lives in the write-ahead log, so a copy of `frontier.db` alone is stale: with 4 MB of log it opened without error and was missing 27% of the ledger. Say "stop the server first, or never copy the `.db` alone". A `backup` command fits with plan item 7.2; it should use `VACUUM INTO` from a second read-only connection (4 ms, no effect on 16 live sessions) and must never take a write lock. (OPS-8)

### 2.11 Suggested order

1. 2.1 and 2.2 together, then `overflow-checks`, then CI on the release profile.
2. The end rule (8.1), then 2.3.
3. 2.4, 2.5, 2.7, 2.8 and the table in 2.9, in any order.
4. The load bot and lag signals, then 2.6, tested on the impaired link.

## 3. Seams to keep

Small changes now that keep a later option open. None builds a Stage 2 system.

| Seam | Keeps open | Now |
|---|---|---|
| Persistence stays behind `store.rs`: `rusqlite` appears nowhere else, and callers use 13 `store.call` sites. Isolation today comes from the process-wide mutex, not the database. (DB lens) | A move to a server database touches one file and the shape of `call`; `Store::update(id, closure)` maps onto `SELECT … FOR UPDATE` | Add nothing outside `store.rs` that speaks SQL. A `settled(run, account)` key (2.3) is wanted on any engine |
| `impl SnapshotSink for Session` lives in `expedition.rs:38`, `:57-68`, the one place the instance module depends on the transport. (OPS-13) | An instance host separate from the connection gateway | Move the impl to `transport.rs` |
| The client learns the WebTransport address from `/api/connect`. (OPS-13) | A gateway that hands out addresses | Keep the indirection |
| The public WebTransport setting is a `SocketAddr`, and `bind` takes only `DevCertificate`. (OPS-6) | A host name and a CA certificate, which plan item 7.3 needs | Make the setting a `host:port` string; make `bind` take a chain and key |
| The pinned certificate lasts 13 days and nothing says when it lapses. A documented Stage 1 limit of the development path, which plan item 7.3 retires. (OPS-5) | An operator who can see why new connections fail | Log the expiry at start and an error when it passes. Do not build rotation |
| `Store::update` opens a deferred transaction, reads, then writes. A write lock held by another process makes that fail at once, and the session is closed: 27 of 60 trading sessions dropped during a 0.5 s foreign lock. (from the OPS challenge) | Running a report, backup or `wipe` beside a live server | `BEGIN IMMEDIATE` for read-then-write transactions, so they wait instead of failing |
| One material per hull, per silhouette and per bolt part: 2 draw calls per ship. (CLI-3) | Instanced rendering at the Stage 2 gate, measured at 87 draws for 280 ships against 1,883 | Cache materials per allegiance; share bolt geometry; keep `shipMeshes()` and `#drawShots()` the only places that create per-entity objects. Merge hull GLBs by material when they are first loaded: `frigate.glb` is 45 mesh nodes |
| Connection listeners cannot be removed. (CLI-9, NET-10, QUAL-11) | Long sessions without leaks | Return an unsubscribe function from `onMessage` and `onDatagram` |
| Rules written twice: the server re-derives the sim's wave forecast, and the client mirrors outpost rules and about ten constants. (QUAL-6) | Balance changes that cannot drift | `Run::forecast()` in the sim; export the constants from the existing `version.ts` writer; Rust-written preview vectors for `options.ts` |
| Adding a message touches six places, including a 23-variant list in `handshake`. Materials are a bare `[u32; 3]` banked by position. (QUAL-8) | New messages, goods and ship kinds | At the next `PROTOCOL_VERSION` bump, nest home and crew requests; generate `index.ts`; bank through `Good::from_material` |
| `client/src/main.ts` changed in 20 of the last 60 commits, holds nine module-level mutable bindings and has no tests. (QUAL-7) | More panels and message types without one file every feature edits | Extract a tested reducer from `ServerMessage` to client state |
| `run.rs` is 1,796 lines of code with separable checkpoint, presence, threat and objective parts; `expedition.rs` holds wire translation that belongs in `wire.rs`. (QUAL-9) | Every Stage 1 rule still to add | Split as pure moves, one pull request each |
| Refusals are strings, and every session error closes as "protocol violation". (QUAL-10) | Counting refusals by cause; client logic that branches on why | A `HomeRefusal` enum; a distinct close code for server faults |
| The caps benchmark drives a placeholder skirmish with 60 rocks, not a `Run` on the shipped site with 220. The real tick is about twice its figure. (QUAL-5, SIM-3) | A number of record that matches what the server runs | Drive it from `Run::launch` with `opening_threat`; keep the snapshot-size test |
| Rigs and modules are created and destroyed outside the ledger; value lost per defeat exists only in log lines. (DB-6) | The design §7 measure "value lost per defeat" | A ledger item kind for fittings, or one row per pilot per run end |
| Piece identity is dropped at launch; loss is by kind. (DB-7) | Wear and player trade (design §10: "items have identities") | Carry piece ids through the launch order and the checkpoint |
| The frame-time readout measures only the animation callback: 1 ms shown against about 19 ms of main-thread time under software rendering. (CLI-7) | A benchmark figure that means what it says | Label it "script"; expose draw calls and dropped frames as `data-*` |
| e2e steers by timed key holds and 17 `waitForTimeout` calls. (QUAL-4) | A blocking suite that does not flake as it grows | Steer on `data-x` and `data-z` with `expect.poll` |

## 4. Deferred to gates

**The first build reachable by clients the owner does not control.** Plan item 7.3 (a second machine) is the earliest candidate; see question 8.6.

- Bound the per-session frames channel. An unauthenticated connection grows the server by about 1.2 MB a second at 10 Mbit/s, and a client that reads every reply still grows it. A tested fix exists: `mpsc::channel(4)` with the send awaited in the reader task held memory flat and passed 16 of 16 browser tests. (SRV-2; high at S-many)
- Bound the input path with `try_send`, and cap `waiting` by length, keeping the newest. Never put `Disconnect`, `Join` or `Reconnect` on a droppable path. (SRV-3, NET-5)
- Set transport limits and deadlines: stream counts, receive windows, a deadline for hello and sign-in, a session cap, per-session message budgets. Tested against Chromium: 4 bidirectional and 4 unidirectional streams with a 128 KiB stream window pass; fewer than 2 and 3 fail the handshake. (SRV-8, OPS-7)
- 2.1 must already be done; it is critical here.
- A CA certificate, a host name and HTTPS for the page; choose the passkey relying-party identifier deliberately, since a passkey cannot move to another one. (OPS-6)
- Compression and cache headers: a cold load is 2.66 MB uncompressed and about 397 kB gzipped. (CLI-8, OPS-11)
- Egress: 12–33 Mbit/s for 300 pilots, the first resource to run out on a home connection; two agents computed it independently and neither measured it on a wire. The cheap levers are a leaner entity record, then far entities at 10 Hz. (NET-6)
- Measure through Docker Desktop's UDP forwarding, the documented way to host, which no load test here covered.
- Rotate resume tokens and add the two missing indexes. (DB-8)
- Cap the pixel ratio; reject unknown `FRONTIER_*` names; log a build identity. (CLI-11, OPS-14)

**The first data that may not be wiped.**

- Ordered schema migrations; today a schema change means bump and wipe. (DB-9)
- Scheduled, off-machine backups; account recovery when a passkey is lost.

**Stage 2 bounded PvP (S-site).**

- The snapshot. A leaner entity record (14 bytes to about 10, dropping velocity that duplicates heading and speed) raises the ceiling to 76 entities, which covers 40 ships plus 24 NPCs in one datagram with no architectural change. Take that first. (NET-7)
- A projectile budget set from a measured fight, with fairness when it binds: today the ships that spawned first always fire first. Demand peaked at 59 for 40 pilots plus 60 NPCs. (SIM-5)
- Hostility by side, not "pilot against everything else": seven sites in the sim. (SIM-7)
- Instanced ship and bolt rendering on the client. (CLI-3)

**Stage 2+ shared regions (S-region).**

- Interest management, delta encoding or datagram splitting. The header reserves nothing for them and need not: a new layout is one coordinated version bump. (NET-7)
- Ship identifiers that wrap at 65,536 spawns, a `NO_SHIP` sentinel that is also a legal identifier, and positions limited to ±1,024 m. A Stage 1 instance spawns at most about 5,000. (SIM-4, NET-12)
- The `Joined` message, which reaches its 64 KiB cap at about 1,310 obstacles. (NET-11)
- A second server process. The registries of runs, sessions and crews are process-local. This, not user count, is the trigger for a server database (question 8.4).
- A read-only connection for reports and backups, so a long query does not stall the writer. (DB-10)

**Plan milestone 7.1.** Write run and phase events to a table in the store instead of parsing log lines. (OPS-12)

## 5. What is sound

Checked and found good, so it need not be re-examined.

- **The instance model.** One task owns one `Run`; nothing in the simulation is shared. No mutex guard crosses an `.await`, locks are never nested, and every store call from async code goes through `spawn_blocking`. A slow client cannot stall an instance.
- **Simulation cost, at every scenario.** Leave the pair loops, linear lookups and allocation alone. Seeded generation is deterministic: 2,000 seeds hashed identically on x86-64 and wasm32.
- **`frontier-sim` does no I/O and reads no clock.** Grep finds no time, filesystem, environment, network or unseeded randomness in the crate.
- **The wire format.** S1 at its own caps fits one datagram with 36% to spare (637 of 1,000 bytes). Both decoders are total and held together by Rust-written test vectors. Input redundancy recovers three consecutive lost datagrams. Prediction runs the same movement code as the server.
- **SQLite for S-many.** WAL with `synchronous=FULL` is the right setting for an economy. Nothing writes per tick. Balance changes and ledger rows share a transaction; the ledger is append-only by trigger.
- **Client frame cost at S1.** 67–80 draw calls and about 1.5 ms of script per frame at the NPC cap. No per-frame raycasts, no shadows, no post-processing. The wasm boundary costs 0.43 µs per call.
- **Code hygiene.** Clippy passes with `-D warnings`; no `unwrap`, `todo!` or `TODO` in non-test code; no `any` or non-null assertions in the TypeScript; typed errors throughout; workspace dependencies declared once. Every one of the last 25 behaviour-changing commits added tests.
- **Decisions under AGENTS.md §8.** WebTransport without fallback, JSON streams with binary snapshot datagrams, embedded SQLite, passkeys and wasm prediction: no new evidence against any of them for Stage 1.

## 6. Rejected and disputed

Of 33 findings and claims put to challengers, 9 were confirmed as rated, 22 were confirmed with a lower severity or a narrower scope, and 2 were rejected. The client lens had no challenger of its own; its one high finding was tested as NET-1.

| Finding | Reviewer | Challenger | Reason |
|---|---|---|---|
| The client sends at most one input per frame; use a catch-up loop (NET-8) | Low, fix now | **Rejected** | The server has already filled the missed ticks. A catch-up loop adds a lasting 50 ms per client hitch; the current behaviour is the only thing that drains the backlog |
| A cleanly departed pilot is never dropped by the snapshot sink (SRV-10, item 3) | Low | **Rejected** | The session task detects the close and sends `Disconnect` 43 µs later; the sink's return value never comes into play |
| Ignore input sequences too far ahead (part of NET-5) | Fix now | **Fix rejected** | It kills a pilot's input for good after an uplink blackout of about 350 ms. Cap the queue's length instead |
| Unbounded memory from one connection (SRV-2) | Critical | High, S-many only | Real and unbounded, but the reported 5 GB in 12 s needed a 3.5 Gbit/s loopback. No honest Stage 1 client can do it |
| Input datagram flood (SRV-3) | High, fix now | Medium, defer | Needs about 100,000 four-input datagrams a second. The honest client sends 20; Chromium pushed flat out reached 47,000 |
| Trade overflow (QUAL-1 and three others) | Critical | High at S1 | Needs a hand-crafted frame from an invited tester; data is wipeable. Critical once exposed |
| One-way interpolation clock (NET-1, CLI-1) | High and medium | Low | Two skipped ticks cost the jitter buffer, but the result is heading judder; stepping needs five. A 250 s run on a file-backed store skipped no ticks |
| The 54-entity snapshot ceiling (NET-7) | High at S-site | Informational | Unreachable at S1: every spawn path is capped and the maximum is 29. The proposed overflow seam is code for a state Stage 1 cannot reach |
| The NPC cap contradicts the design's crew table (SIM-1) | Medium | Low | The nine wardens are the code's number, not the design's. Raising the cap to 40 contradicts the published 24 and still binds. Left as a question (8.2) and two small fixes (2.9) |
| Ship identifier wrap (SIM-4) | High at S-region | Low | Reproduced, but rates a world that is not designed. A documentation line now |
| The tick loop awaits the store (DB-4, SRV-7) | Medium | Low | At 50 times the estimated load on a slow disk, no write outlasted a tick |
| Logging blocks the runtime (SRV-4) | High | Medium | Needs a stalled log reader, and heals itself |
| Poisoned store lock (QUAL-2) | High | Medium | The only reachable trigger is 2.1 in a dev build |
| The caps benchmark measures a placeholder (QUAL-5) | Medium, fix now | Low, tidy | Keeping the skirmish as the fixture was deliberate; no sizing decision changes |
| Transport limits (SRV-8) | Fix now | Defer | No honest Stage 1 client touches any of it |
| Stale snapshots after a blackout (NET-4) | Medium | Low | Worst staleness 7 s, not 108 s; the client half is already handled by the browser's datagram queue |
| No server-side lag signals (OPS-3) | Medium at S1, fix now | Low at S1 | The absence is confirmed, but the HUD shows round trip and correction, and the unexplained hitches were a whole-process stall while other agents compiled, not the checkpoint. Ten lines of `warn!` now; metrics at the scale gate |
| The 13-day certificate (OPS-5) | High at S-many; keep a rotation seam | Low; no seam | A documented limit of a path plan item 7.3 retires. "High at S-many" counts OPS-6 twice |
| No backup path (OPS-8) | Medium, fix now | Low at S1 | Data is wipeable and a clean stop yields a complete file. A README line now; the command with plan item 7.2 |
| CI does not build what ships (OPS-9) | Medium | Low | The harm is 2.1, already counted. Take the one-flag change; the tick tripwire flakes in debug |
| After a restart the client's reload strands the page with no retry (OPS-4, client half) | Medium | Low | True of the headless test browser only. A full Chromium retries its own error page and recovered unaided |
| The load test's "combat" run (OPS, run `many-e`) | 1,173 run-overs in combat | Not combat | The bot flew a circle and re-docked. The challenger's own combat pass (931 ships destroyed in 80 s) cost the same 36% |

Dissent that stands: the server reviewer would fix the transport limits and the hello deadline now, because they are a few lines. The challenger defers them because no Stage 1 client reaches them. This report follows the challenger and lists them at the exposure gate.

## 7. Measurements

All on one 4-core container shared between the reviewers, so tails are pessimistic and medians are the figures to read. Client numbers come from headless Chromium on software rendering and are indicative only. Network numbers are over loopback. One reviewer briefly filled the shared disk during an experiment; no result reported here was taken in that window.

**Simulation** (release; `World::step` unless stated)

| Ships | p50 | Share of the 50 ms tick |
|---|---|---|
| 28 (4 pilots, 24 NPCs, 220 rocks) | 18.9 µs | 0.04% |
| 64 | 49.9 µs | 0.10% |
| 100 | 97 µs | 0.19% |
| 200 | 237 µs | 0.47% |
| 400 | 718 µs | 1.4% |
| 800 | 2,175 µs | 4.4% |

- Challenger's benchmark, ships clustered in combat with AI live; within 1.3× of the reviewer's.
- `cargo run --release -p frontier-server --example caps`: median 9.0–9.3 µs, p99 23–25 µs; snapshot bytes median 432, maximum 468 in every run.
- Whole S1 tick (step, snapshot, four encodes): 20.4 µs. 100 instances stepped back to back: 2.25 ms, 4.5% of one core, as an upper bound.
- Heap: 5.7 allocations and 1.5 kB per `World::step`.

**Server**

- In process, 100 instances of 4 pilots at the NPC cap: 20.0 snapshots a second each; tick gap p50 50.0 ms, p99 51.0 ms, maximum 54.1 ms; 9.7% of one core.
- Load bot against the release binary over real WebTransport (challenger's passes; the reviewer's first pass gave 31%):

| Pass | Server CPU (one core) | Memory | Snapshot gap p50 / p99 / max |
|---|---|---|---|
| 4 pilots, 1 instance | 1.2% | — | maximum 53.7 ms |
| 300 pilots, 100 instances, at the dock | 36.0% | 37 MB | 50.0 / 51.2 / 60.1 ms |
| 300 solo pilots in combat, relaunching | 36.1% | 39 MB | 50.0 / 51.1 / 66.6 ms |
| 600 pilots, 150 crews of 4 | 50.8% | 61 MB | 50.0 / 51.2 / 72.8 ms |
| 300 pilots, 40 ± 10 ms delay and 2% loss each way | 47.3%, relay included | 43 MB | 50.1 / 100.2 / 196 ms |

- Every pilot saw 20.00 ticks a second in the unimpaired passes. The simulation is about 2 points of the 36; transport is the rest.
- 2,000 sessions that never said hello: all open after 75 s, 114 MB.
- SIGTERM to exit: 5–16 ms with up to 300 sessions.

**Persistence** (file-backed, through the public `Store` methods)

- Write load at S-many, counted from the code: 6–10 transactions a second.
- Fast pass (fsync 0.16 ms): 4,000–8,000 transactions a second on one connection. Slow pass (fsync 3.3 ms): 370–670.
- Open loop on the slow pass: at 450 a second, p99 13.7 ms and no write over 50 ms.
- Sizes: account document 294–947 bytes; checkpoint 629 bytes for one pilot, 1,372 for four; ledger 73 bytes a row.

**Network**

- Snapshot bytes = 23 + deposits + 14 × entities + 6 × events. S1 worst case 637; ceiling 54 entities.
- Per client at 20 Hz: 78 kbit/s at the measured median, 111 kbit/s worst case.
- Input age on the real queue, modelled client: 34 ms clean; 136 ms within 30 s at jitter 0–20 ms with 2% loss.

**Client**

- Real client: home 383 draw calls; flight at the NPC cap 67–80; animation callback 1.5–1.7 ms mean.
- Scale harness, per-mesh against instanced: 28 ships and 64 bolts, 263 draws against 87; 280 and 640, 1,883 against 87.
- Bundle: three.js 655 kB (165 kB gzipped), main 52 kB, wasm 31 kB; `station.glb` 1.16 MB, `facilities.glb` 0.75 MB, each about 97 kB gzipped.

**Code and CI**

- 20,050 lines of Rust and 8,932 of TypeScript; 347 Rust tests and 192 Vitest tests.
- `cargo clippy --workspace --all-targets -- -D warnings`: clean. With `arithmetic_side_effects`: 88 sites in non-test code.
- CI on `main`: 6 min 12 s wall; e2e 6 min 08 s, rust 2 min 31 s.

**Not measured.** Real-GPU frame times; real network cards, WAN links and browsers under load; Docker Desktop's UDP forwarding; any load run longer than 80 seconds, so no 45-minute instance and no check for memory growth; more than one or two runs per configuration. Read the load figures as "one process has several-fold CPU headroom for 300 pilots on Linux", not as a WAN capacity figure.

## 8. Open questions for the owner

1. **When should a crew instance end?** Today it ends when someone is over and everyone else is disconnected, which abandons the last pilot flying the moment their link drops (2.3). The challenger's suggestion: end early only when a connected pilot finishes and leaves nobody connected and flying. Design §10's "every connected pilot" does not settle it.
2. **What happens when the NPC cap binds?** Drop, queue or scale down, and do wardens count inside the opening threat? At tier III with four pilots and the nest open, four of nine wardens are never placed. The design publishes the cap and the scaling but not the rule.
3. **Should queued inputs count towards the supplies price?** A pilot with 30 credits and 20 ore cannot launch, but can after queueing the ore for processing, because `supplies_due` charges what is in stock. The design states only a flat 40 credits.
4. **Is PostgreSQL decided, and what triggers it?** The owner stated during this review that a later move to PostgreSQL is planned. No document in the repository says so: `AGENTS.md` §8 and the Stage 1 plan record embedded SQLite as decided. The measurements here say user count is not the trigger; a second server process, online reporting and operational tooling are. If it is decided, record it in §8 with its trigger.
5. **After a store panic, recover or exit?** This report recommends recovering the lock (2.2). Exiting for a supervisor to restart is the honest alternative, and needs 2.3 fixed first.
6. **Which gate admits clients the owner does not control?** The first list in section 4 is due before it. If plan item 7.3 puts testers on a network the owner does not fully control, that gate is closer than Stage 2.

## Appendix: findings by lens

Final rating after challenge, and where each finding landed. "Unchallenged" means the reviewer's rating stands untested.

| Finding | Subject | Final | Section |
|---|---|---|---|
| SIM-1 | NPC cap against crew scaling | Low | 2.9, 8.2 |
| SIM-2 | In-site lock and the disconnect timer | Low, confirmed | 2.9 |
| SIM-3 | Caps benchmark under-reports | Low | 3 |
| SIM-4 | Ship identifier wrap | Low | 4 |
| SIM-5 | Projectile cap ordering | Medium at S-site, unchallenged | 4 |
| SIM-6 | Quadratic loops, measured | Leave alone, confirmed | 5 |
| SIM-7 | Hostility model | Low, unchallenged | 4 |
| SIM-8 | Ships inside close rocks | Low, cosmetic, unchallenged | — |
| SRV-1 | Trade overflow | High at S1 | 2.1 |
| SRV-2 | Unbounded frames channel | High at S-many | 4 |
| SRV-3 | Input datagram flood | Medium | 4 |
| SRV-4 | Blocking log writer | Medium | 2.8 |
| SRV-5 | Crew run ends on restore or drop | Medium, confirmed and widened | 2.3 |
| SRV-6 | Bank and checkpoint separate; no shutdown save | Medium | 2.3 |
| SRV-7 | Tick loop awaits the store | Low | 6 |
| SRV-8 | No admission limits | Medium, deferred | 4 |
| SRV-9 | Store panic | Medium | 2.2 |
| SRV-10 | Connection bookkeeping by hand | Low; item 3 rejected | 6 |
| SRV-11 | Follower charged before join | Medium, confirmed | 2.4 |
| SRV-12 | Stale snapshot queue | Low | 2.9 |
| SRV-13 | Crew and startup details | Low, unchallenged | 2.9 |
| NET-1 | One-way interpolation clock | Low | 2.9 |
| NET-2 | Latched input | Medium, confirmed | 2.5 |
| NET-3 | Input delay ratchet | Medium, confirmed | 2.6 |
| NET-4 | Datagram send buffer | Low | 2.9 |
| NET-5 | Peer-driven growth | Deferred; one fix rejected | 4, 6 |
| NET-6 | Egress at S-many | Medium, unchallenged | 4 |
| NET-7 | 54-entity ceiling | Informational | 4 |
| NET-8 | One input per frame | Rejected | 6 |
| NET-9 | Events are lossy | Leave alone, unchallenged | — |
| NET-10 | Silent frame failure; listener leak | Nit | 3 |
| NET-11 | `Joined` size | Low, unchallenged | 4 |
| NET-12 | Identifier and coordinate range | Low, unchallenged | 4 |
| DB-1 | Trade overflow | High at S1 | 2.1 |
| DB-2 | Settlement not atomic with checkpoint | Medium at S1, high at S-many | 2.3 |
| DB-3 | Store panic | Medium | 2.2 |
| DB-4 | Tick loop awaits the store | Low | 6 |
| DB-5 | Follower charged before join | Medium | 2.4 |
| DB-6 | Fittings outside the ledger | Low, unchallenged | 3 |
| DB-7 | Piece identity dropped | Low, unchallenged | 3 |
| DB-8 | Session tokens accumulate | Low, unchallenged | 4 |
| DB-9 | No migration path | Low, unchallenged | 4 |
| DB-10 | One connection for reads | Low, unchallenged | 4 |
| DB-11 | `pay_supplies` composition; queued inputs | Nit; second half confirmed | 2.4, 8.3 |
| CLI-1 | One-way interpolation clock | Low (as NET-1) | 2.9 |
| CLI-2 | Event queue and bolt pool | Medium, unchallenged | 2.9 |
| CLI-3 | Draw calls per ship | Medium at S-site, unchallenged | 3, 4 |
| CLI-4 | Station as 379 meshes | Low, unchallenged | 2.9 |
| CLI-5 | Lock bracket material | Low, unchallenged | 2.9 |
| CLI-6 | HUD DOM churn | Low, unchallenged | 2.9 |
| CLI-7 | Frame-time readout | Low, unchallenged | 3 |
| CLI-8 | Cold load size | Medium at S-many, unchallenged | 4 |
| CLI-9 | Listeners not removed | Low, unchallenged | 3 |
| CLI-10 | Per-frame allocation | Leave alone, unchallenged | — |
| CLI-11 | Pixel ratio | Low, unchallenged | 4 |
| CLI-12 | Small items | Low, unchallenged | — |
| QUAL-1 | Trade overflow | High at S1 | 2.1 |
| QUAL-2 | Store panic; session cleanup | Medium | 2.2 |
| QUAL-3 | Follower charged before join | Medium, confirmed | 2.4 |
| QUAL-4 | SIGTERM race; timing-based tests | Medium, confirmed | 2.7, 3 |
| QUAL-5 | Caps benchmark on a placeholder | Low | 3 |
| QUAL-6 | Rules written twice | Medium, unchallenged | 3 |
| QUAL-7 | `main.ts` | Medium, unchallenged | 3 |
| QUAL-8 | Shotgun surgery | Medium, unchallenged | 3 |
| QUAL-9 | Large files | Low, unchallenged | 3 |
| QUAL-10 | Stringly refusals | Low, unchallenged | 3 |
| QUAL-11 | Smaller items | Low, unchallenged | 2.9, 3 |
| QUAL-12 | Behaviours with no test | Low | with each fix |
| OPS-1 | Trade overflow; release against debug | High at S1 | 2.1 |
| OPS-2 | Store panic; no health signal | Medium | 2.2 |
| OPS-3 | No lag signals | Low at S1 | 2.10 |
| OPS-4 | Stop and restart | Medium at S1; client half low | 2.3 |
| OPS-5 | 13-day certificate | Low | 3 |
| OPS-6 | No path to a real certificate | Medium, unchallenged | 3, 4 |
| OPS-7 | No admission control | High at S-many, deferred | 4 |
| OPS-8 | No backup path | Low at S1 | 2.10 |
| OPS-9 | CI does not build what ships | Low | 2.10 |
| OPS-10 | No load harness | Medium, unchallenged | 2.10 |
| OPS-11 | No cache policy or compression | Low at S1 | 4 |
| OPS-12 | Run events only in logs | Low | 4 |
| OPS-13 | Process-local state | Low | 3, 4 |
| OPS-14 | Configuration gaps | Low | 4 |
