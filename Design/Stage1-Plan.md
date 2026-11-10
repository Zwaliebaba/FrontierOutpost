# Stage 1 implementation plan

**Draft 1 · 10 November 2026 · For owner review**

This plan turns Stage 1 of the [game design](FrontierOutpost.md) (§9, with the values in §10) into
an order of work. It follows the decisions recorded in [AGENTS.md](../AGENTS.md) §8. Where the
plan and the design disagree, the design wins and the plan is corrected.

## 1. How the work is cut

The owner directs and reviews; AI coding agents write most of the code. So the unit of work is a
**work item**: one change an agent can finish in a session and the owner can review as one pull
request. Every work item names the check that proves it. A work item that cannot name one is too
big or too vague and is split first.

Each milestone ends in something the owner can run and judge, not in a layer of code. The order
puts the riskiest questions first where the owner's decisions allow it.

## 2. Decisions the plan rests on

| Area | Decision |
| --- | --- |
| Runtime and HTTP | Tokio; axum inside the game server serves the built client |
| Transport | WebTransport on quinn and `web-transport-quinn`; no fallback |
| Wire format | JSON on reliable streams; snapshots as binary datagrams of at most 1,000 bytes |
| Prediction | `frontier-sim` movement compiled to WebAssembly and run in the browser |
| Identity | Passkeys |
| Persistence | Embedded SQLite with an append-only ledger; Stage 1 data may be wiped |
| Simulation | Fixed ticks, no I/O; sites reproducible from a seed; no session replay |
| Camera and input | The greybox defaults; mouse and keyboard; touch after the Stage 1 gates |
| Art | Sockets and facility modules authored through `create_scene.py` |
| Playtests | About eight invited testers, on machines the owner provides |
| Licence | All rights reserved for now |

## 3. Milestones

### M0 · Foundations

*At the end: the real server and client exchange messages over WebTransport, in CI.*

| # | Work item | Proof |
| --- | --- | --- |
| 0.1 | Add Tokio, quinn, `web-transport-quinn` (with the `ring` backend) and axum to the workspace | The workspace builds and passes clippy on Linux and Windows CI |
| 0.2 | Server: accept a WebTransport session with a certificate generated at start-up; axum serves the client and the certificate hash | A Rust client test connects and completes the existing Hello/Welcome handshake |
| 0.3 | Client: a `net` module that connects, sends and receives JSON messages on streams, and exposes a datagram channel | A browser test in CI completes the handshake against the real server |
| 0.4 | Snapshot codec in `frontier-protocol`: binary encode and decode in Rust, decode in TypeScript, from shared test vectors | Round-trip tests in both languages; a test fails if 4 ships and 24 NPCs exceed 1,000 bytes |
| 0.5 | WebAssembly build of `frontier-sim`, loaded by the client, in CI | The client calls a `frontier-sim` function in a browser test |
| 0.6 | ~~Run the transport spike in Firefox and Safari~~ Dropped by the owner; the spike is deleted | Firefox and Safari stay untested, an accepted risk |

### M1 · Fly

*At the end: you steer a ship on the real server, and it stays responsive with delay and loss
injected.*

| # | Work item | Proof |
| --- | --- | --- |
| 1.1 | `frontier-sim`: world state, ship movement by thrust and turn, fixed 20 per second tick | Unit tests for movement, limits and tick stepping |
| 1.2 | Server: an expedition instance that takes intents and sends snapshots; any number of clients up to four from the start | An integration test with two clients sees both ships move |
| 1.3 | Client: draw ships from snapshots, interpolated 100 ms behind, with the greybox camera, look, zoom and picking promoted into the client | Vitest for interpolation; the greybox still passes its tests |
| 1.4 | Client: predict the player's own ship with the WebAssembly rules and reconcile against snapshots | A test replays recorded snapshots with delay and asserts a bounded correction |
| 1.5 | A development switch that routes traffic through a delay-and-loss relay | The owner flies at 150 ms and 250 ms round trip and records the result |

### M2 · Fight

*At the end: you lock, shoot and are shot by NPCs, and can be destroyed.*

| # | Work item | Proof |
| --- | --- | --- |
| 2.1 | Selection that locks: click a ship, cycle to the nearest hostile, centre the camera when not steering | Vitest for selection state; a browser test locks a target |
| 2.2 | Weapons: lock-on fire, hits resolved by the server, projectiles sent as events and drawn by the client | Sim tests for range, cooldown and damage |
| 2.3 | NPCs: drone, raider and warden behaviours from §10 | Sim tests for each behaviour's target choice |
| 2.4 | Destruction and its outcome: cargo and upgraded modules lost, hull reissued, pilot returned home | Sim test of the §6 outcome table |
| 2.5 | Readability rules in the real client: one render height, allegiance by shape and colour, silhouettes through structures | The greybox's measures run against the real renderer |
| 2.6 | Benchmark at the provisional caps of 24 NPCs and 64 projectiles | A recorded frame-time and snapshot-size result |

### M3 · Extract

*At the end: a solo pilot can complete a tier I run from launch to dock. This is the first build
in which the game's loop can be judged.*

| # | Work item | Proof |
| --- | --- | --- |
| 3.1 | The site as data: template, pockets, seed parameters, threat tiers | The same seed gives the same site in a test; a second seed differs |
| 3.2 | Extraction: rates, heat, cut-out and manual venting; nothing yields without input | Sim tests, including that an unattended extractor stops |
| 3.3 | Alert, waves and escalation with time and heat; return-leg interception by cargo carried | Sim tests against the §10 tables |
| 3.4 | Cargo, the approach and return legs, docking banks cargo, the instance ends | An integration test completes a scripted run |
| 3.5 | Scanning reveals the hidden seed values | Sim test; the client shows what was revealed |
| 3.6 | Phase timers for preparation, travel, objective and unloading | The timers appear in the run's event log |

**Checkpoint.** The owner plays the loop before M4 starts. The review's largest risk was that
extraction is labour, and the owner chose not to prototype it separately, so this is where that
risk is first seen. If the loop is dull, it is reworked here, before accounts and the outpost are
built on it.

### M4 · Persist

*At the end: you sign in, and your home survives a server restart.*

| # | Work item | Proof |
| --- | --- | --- |
| 4.1 | SQLite storage: versioned account documents with a world identifier | A test loads a document written by the previous schema version |
| 4.2 | The ledger: every change to stock goes through one transfer function and is written in the same transaction as the save | A conservation test: the ledger folded equals the stock saved |
| 4.3 | Passkey registration and sign-in, carried over a stream; a session token for reconnection | A browser test registers and signs in with a virtual authenticator |
| 4.4 | Reconnection within the grace window; the disconnect exposure timer; pocket checkpoints restored after a server restart | An integration test kills and restarts the server mid-run |
| 4.5 | A command that wipes Stage 1 data | A test runs it and finds an empty world |

### M5 · Outpost

*At the end: a completed run buys a facility that visibly changes the outpost and opens something
new on the next run. This is the game's signature.*

| # | Work item | Proof |
| --- | --- | --- |
| 5.1 | `create_scene.py`: construction sockets and three facility modules at two tiers, exported separately | The manifest lists them; the owner runs Blender and commits the regenerated assets |
| 5.2 | Client loads the station and module models; CI fetches the real asset files | A browser test finds each socket and module in the loaded scene |
| 5.3 | Outpost rules as data: facilities, capability tags, power cap, sockets, adjacency, reinstallation cost | Sim tests; a test adds a facility through data alone |
| 5.4 | Outpost view: free orbit, select a facility, build, upgrade and reconfigure, with previews that name what opens | Browser tests for build and reconfigure |
| 5.5 | Capabilities reach the expedition: pockets, rigs, modules and threat tiers follow the powered layout and unlocks | An integration test shows a locked pocket opening after a purchase |
| 5.6 | Trade and processing: fixed NPC prices, recipes that advance per completed run, the launch cost | Sim tests; a data test that no recipe is an arbitrage |
| 5.7 | Fittings and the launch screen, showing the stake at replacement price | Browser test of the stake shown |

### M6 · Crew

*At the end: two to four players run an expedition together under the crew rules.*

| # | Work item | Proof |
| --- | --- | --- |
| 6.1 | Invite code, lobby, the host chooses contract and tier | An integration test with two accounts joins by code |
| 6.2 | Shared access, threat and yield scaling, the payout pot and its split | Sim tests against the §10 crew table |
| 6.3 | The two-ship relay objective | Sim test |
| 6.4 | A destroyed pilot returns home while the crew continues; a disconnected pilot's run is abandoned if the instance ends | Integration tests for both |

### M7 · Measure and playtest

*At the end: the four Stage 1 gates have results.*

| # | Work item | Proof |
| --- | --- | --- |
| 7.1 | Gate events: selections, losses and relaunches, layouts and reconfigurations, choices per seed | Each §9 measure can be computed from one recorded session |
| 7.2 | A report that computes the gate measures from the database | It reproduces hand-checked figures from a fixture |
| 7.3 | Playtest machines: each prepared to trust the server's certificate and reach it by name | A second machine signs in and joins a run |
| 7.4 | A playtest script, including the veteran, newcomer and two-client comparison | Reviewed by the owner |
| 7.5 | Run the playtests and write up each gate against its provisional threshold | The write-up |

## 4. Size

The plan has 44 work items. Some will split once work starts, so expect 55 to 75 pull requests.
The plan gives no dates: with agents writing the code, the pace is set by how many pull requests
the owner can review in a week. Once M0 and M1 are done, their actual pace gives a real figure
for the rest.

## 5. Risks

| Risk | Where it shows | What the plan does |
| --- | --- | --- |
| Extraction is not fun | M3 | A hard checkpoint before anything is built on the loop |
| Snapshots do not fit 1,000 bytes | M0.4, M2.6 | A size test from the first codec; projectiles travel as events, not in snapshots |
| Prediction feels wrong under latency | M1.4, M1.5 | Measured with injected delay and loss before combat is built on it |
| Firefox or Safari fail the transport | In play | Not tested, by the owner's decision; with no fallback, a failure reopens the transport decision |
| `web-transport-quinn` is pre-1.0 and changing | Throughout | Keep it behind the transport interface and pin its version |
| Windows builds | M0.1 | Proven in CI before anything depends on it |
| Facility art waits on Blender | M5.1 | It has no code dependency and can start any time after M0 |
| Passkeys on shared playtest machines | M7.3 | See open item 1 |

## 6. Open items for the owner

1. **Passkeys on machines the owner provides.** A passkey lives in the operating-system account
   of the machine it was made on. On shared playtest machines every tester's passkey would sit in
   the owner's account, behind the owner's PIN or fingerprint. Options: a hardware security key
   per tester, a separate operating-system account per tester, or a playtest-only sign-in that
   bypasses passkeys.
2. **Blender.** The agents' environment has no Blender, so 5.1 produces a script change that the
   owner runs. Regenerated binaries are committed deliberately (AGENTS.md §6).
3. **The copyright holder's name** in `LICENSE`.
4. **The touch build's stage** is still open in the design. It does not affect Stage 1.
