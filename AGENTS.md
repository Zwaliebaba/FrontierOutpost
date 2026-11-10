# AGENTS.md

Instructions for AI coding agents and human contributors working on **Frontier Outpost**, a
multiplayer isometric space game with an authoritative Rust server and an HTML5 client
(TypeScript + Three.js). Read this file before changing anything.

## 1. Sources of truth

| Document | Governs |
| --- | --- |
| [Design/FrontierOutpost.md](Design/FrontierOutpost.md) | Game design: scope, rules, delivery gates. |
| [Design/Stage1-Plan.md](Design/Stage1-Plan.md) | Build order, work items and checkpoints for Stage 1. |
| [Design/Visual-Style-Guide.md](Design/Visual-Style-Guide.md) | Art direction, material baselines, camera, scene structure. |
| [Design/FrontierOutpost/](Design/FrontierOutpost/README.md) | The reference Blender scene, its generator, GLB exports and manifest. |

- Build toward **Stage 1** of the design (§9): a persistent home outpost, one 1–4-player
  expedition, NPC threats and NPC trading. Do not start Stage 2+ systems (shared regions, player
  market, access-key marketplace, territory, MMO scale) unless explicitly asked.
- When code and design disagree, say so and ask. Don't silently diverge, and don't edit the
  design documents unless asked to.

## 2. Repository layout

```
Cargo.toml              Rust workspace (members: crates/*)
crates/
  sim/                  frontier-sim: game rules, no I/O
  sim-wasm/             frontier-sim-wasm: frontier-sim exported to the browser via wasm-bindgen
  protocol/             frontier-protocol: client/server wire types → TypeScript via ts-rs
  server/               frontier-server: authoritative server binary
client/                 TypeScript + Vite + Three.js browser client
  src/protocol/         index.ts re-exports the wire types; generated/ is written by ts-rs
  src/greybox/          client-only readability test harness, served at /greybox.html
  src/sim/              loads the WebAssembly simulation; pkg/ is written by wasm-bindgen
spikes/                 Throwaway experiments, each its own Cargo workspace; not built by CI
Design/                 Game design, style guide, Blender scenes (binaries in Git LFS)
.github/workflows/      CI
```

## 3. Commands

Run the same checks CI runs before you push.

```sh
# Rust (toolchain pinned by rust-toolchain.toml; rustup installs it automatically)
cargo fmt --all
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace          # also regenerates client/src/protocol/generated/
cargo run -p frontier-server
FRONTIER_IMPAIR_DELAY_MS=75 cargo run -p frontier-server   # add delay (and _JITTER_MS, _LOSS_PERCENT)
docker compose up --build       # server and built client in a Linux container, http://localhost:8080/

# Client
cd client
npm ci
npm run build:sim               # frontier-sim to WebAssembly; run by dev, build and check
npm run dev                     # Vite dev server
npm run check                   # format:check, lint, typecheck, test, build (= CI)
npm run e2e                     # browser tests against the real server; run `npm run build` first
npm run e2e:container           # the same tests against `docker compose up`
npm run format                  # apply Prettier

# Scene generator (Blender 4.3+ or 5.x); write output outside the repo to verify it
SCENE_OUTPUT=/tmp/scene SKIP_RENDER=1 blender -b --python Design/FrontierOutpost/create_scene.py
```

On Windows the Python launcher is `py`, not `python`.

## 4. Architecture rules

1. **The server is authoritative** (design §5). It validates movement, hits, inventory and
   access. The client sends intents and renders server state; it never decides outcomes.
2. **`frontier-sim` holds the game rules and does no I/O.** It advances in fixed `Tick`s and is
   handed its inputs; it does not read the clock or the network. A site is generated from its
   seed, and the same seed must give the same site (design §10). Replaying a whole session from
   its inputs is not required.
3. **`frontier-protocol` is the only contract between server and client.**
   - Every wire type derives `TS` with `#[ts(export)]` and uses
     `#[serde(tag = "type", rename_all = "camelCase", rename_all_fields = "camelCase")]` for enums.
   - Don't send `u64`/`i64` over the wire: ts-rs maps them to `bigint`, but JSON delivers `number`.
     Use `u32` or a string.
   - Bump `PROTOCOL_VERSION` on any incompatible change.
   - Run `cargo test` and commit the regenerated `client/src/protocol/generated/` together with the
     Rust change. CI fails if they differ. Never edit generated files by hand.
   - Reliable messages travel as JSON on streams. Snapshots travel as compact binary datagrams
     and must fit the browser's datagram limit (1,024 bytes in Chromium). The transport is
     WebTransport, with no fallback (see §8).
   - Binary types (`frontier_protocol::snapshot`) derive only `TS`, not serde: their layout is
     written by hand, and `client/src/net/snapshot.ts` decodes it. Rust tests write test vectors
     to `generated/snapshotVectors.ts`, so a layout change that misses the decoder fails CI.
4. **The client imports wire types only through `client/src/protocol/index.ts`.**
5. **One camera, one plane** (design §5, style guide §6). All activities share one orthographic
   tactical view that rests at 33° elevation (`client/src/camera.ts`). A player may orbit it while
   a control is held, zoom it, and centre it on a selected object; it returns to rest. The
   greybox's default settings are the reference. Ships move on a single gameplay plane: XZ in
   Three.js/glTF (Y-up), XY in Blender (Z-up). Height is presentation only.
6. **Units and axes:** metres; Blender Z-up with ships facing +Y; glTF/Three.js Y-up. Convert
   only at the asset-import boundary.
7. **Instrument the economy from the first build** (design §7). Any code that creates, destroys or
   transfers currency or materials must emit an instrumentation event.

## 5. Code standards

**Everywhere:** UTF-8 and LF line endings (`.editorconfig`, `.gitattributes`). Every behaviour
change ships with a test. Keep comments for the *why*.

**Rust**
- Edition 2024, toolchain pinned in `rust-toolchain.toml`. Formatting comes from `rustfmt.toml`
  (stable options only).
- Clippy must pass with `-D warnings`. Workspace lints forbid `unsafe` and warn on `unwrap()`
  outside tests: use `?` with typed errors, or `expect("why this cannot fail")` for true invariants.
- Declare dependencies once in `[workspace.dependencies]`; crates opt in with `dep.workspace = true`
  and `[lints] workspace = true`.
- Unit tests go in `#[cfg(test)] mod tests` beside the code; cross-crate tests go in `tests/`.

**TypeScript**
- `tsc` runs in strict mode with `noUncheckedIndexedAccess` and `exactOptionalPropertyTypes`.
  ESLint uses `typescript-eslint` `strictTypeChecked`. Prettier uses `printWidth` 100.
- TypeScript is held at 6.0.x until `typescript-eslint` supports 7.x.
- Tests use Vitest and sit next to the source as `*.test.ts`. Browser tests against the real
  server use Playwright and live in `client/e2e/` as `*.e2e.ts`. Keep rendering-free logic (camera,
  math, state) in plain modules so it can be tested without WebGL.

## 6. Assets

- **Binary files are stored in Git LFS** (patterns are in `.gitattributes`). Add the pattern
  *before* committing a new binary type. CI runs `git lfs fsck --pointers`.
- Do not hand-edit `.glb`, `.blend`, `scene_manifest.json` or preview PNGs: change
  `create_scene.py` and regenerate. Regenerating on a different Blender version changes bytes
  without changing geometry, so commit regenerated binaries only on purpose.
- `create_scene.py` must keep working on Blender 4.3+ and 5.x. After changing it, run it with
  `SCENE_OUTPUT` pointing outside the repo and diff the new `scene_manifest.json` against the
  committed one.
- A new scene goes in `Design/<SceneName>/` with the files listed in style guide §8. Record any
  new visual convention in the style guide.
- CI checks out LFS *pointers* only. If the client build starts loading real asset files, enable
  `lfs: true` in the client job.

## 7. Workflow

- Branch from `main`; merge through pull requests. Every CI job blocks: `rust`, `rust-windows`,
  `client`, `e2e` and `repo`. The protection is defined in `.github/rulesets/main.json` and
  imported by hand under Settings → Rules → Rulesets. When a CI job is added or renamed, update
  the ruleset and re-import it; the `repo` job fails until they match. Check the CI run after pushing; a red `main` is a stop-everything event.
- Make small commits, one logical change each, and make sure each one builds and passes tests.
- Bump toolchains (`rust-toolchain.toml`, `client/.nvmrc`) in their own PR.
- Never commit `target/`, `node_modules/`, `client/dist/`, `.env*` or Blender backups (`*.blend1`).

## 8. Open decisions — ask, don't assume

None at present. New ones are added here.

Decided:

- Server async runtime: Tokio. HTTP framework: axum, inside the game server, serving the built
  client.
- Network transport: WebTransport, served by quinn and `web-transport-quinn`, with no fallback.
  Tested in Chromium only. The owner decided not to test Firefox or Safari, accepting that a
  failure there would surface in play and reopen this decision.
- Wire format: JSON on reliable streams for everything except snapshots, which are compact binary
  datagrams. A transport spike (removed; in Git history at commit 3d37c06) showed why: on a
  lossy link, large reliable messages hitch whether or not each has its own stream.
- Identity: passkeys (WebAuthn).
- Persistence: embedded SQLite, with an append-only ledger beside the saves. Stage 1 data may be
  wiped; saves are still versioned.
- Prediction: the browser runs `frontier-sim`'s movement rules compiled to WebAssembly.
- Hosting for Stage 1: the owner's machine. Testers play on machines the owner provides.
- Licence: all rights reserved for now (`LICENSE`).
- Art for Stage 1: sockets and facility modules come from `create_scene.py`, not primitives.
