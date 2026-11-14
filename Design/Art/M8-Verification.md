# M8 delivery and verification

Implementation items 8.1–8.9 are delivered. The owner's balance checkpoint below remains open.
The reference-inspired station rebuild remains M9 work.

| Item | Delivered proof |
| --- | --- |
| 8.1 | Shared contract/material library; new/check/export for all four kinds; 16 Blender tests including valid and deliberately broken fixtures, export/reimport bounds and triangle counts, and unchanged repeat exports |
| 8.2 | Generated catalogue, complete ready-facility coverage, pending-art build refusal, source/export hash checks, eight Blender-free verifier tests, and browser checks that each facility downloads once for both tiers |
| 8.3 | Sensor array, refinery and fabrication bay migrated into separate editable sources, with both tiers and tells; the frozen generator/scene no longer contains them; all other regenerated manifest data matches |
| 8.4 | Two crew-quarters habitat tiers and matching exterior tells, validated and exported through the same pipeline |
| 8.5 | Powered quarters grant wingmen; humans and wingmen share four slots; threat and yield include wingmen while the payout and shares count humans only |
| 8.6 | Follow, marked hold, return, and tier-2 marked-deposit extraction with automatic venting; retaliation uses observed shots, never private NPC locks; an explicit pilot jump can be followed; hidden-seed and movement tests pass |
| 8.7 | Launch charge and ledger event, authoritative orders, distinct snapshot kind and status updates; an integration test links both relays with a solo pilot and wingman and pays only the pilot |
| 8.8 | Launch choice and spare-rig choice; F follow, H mark hold, B return, N mark deposit; order/activity/cargo panel, plane marker and an allied double-ring badge; browser test covers all tier-1 orders |
| 8.9 | Wingman cargo banks to its owner; loss removes its reserved rig, never the pilot's identical rig; repeat banking is harmless; owner/order/fitting reservation survive checkpoints |

## Validation

Verified with the pinned Rust 1.99.1 and Node 26, and Blender 4.3.2:

- `cargo fmt --all --check`
- `cargo clippy --workspace --all-targets --locked -- -D warnings`
- `cargo clippy -p frontier-sim-wasm --target wasm32-unknown-unknown --locked -- -D warnings`
- `cargo test --workspace --locked`: 449 passing tests, including restart/process and wire tests
- `RUSTDOCFLAGS='-D warnings' cargo doc --workspace --no-deps --locked`
- `cd client && npm run check`: 363 passing tests, format/lint/types/build checks
- `cd client && npm run e2e:software`: 46 passing browser tests against real servers
- `blender -b --python-exit-code 1 --python Design/Art/tools/tests/test_frontier_art.py`: 16 passing tests
- `python3 -m unittest discover -s Design/Art/tools/tests -p test_check_exports.py`: eight passing tests
- `python3 Design/Art/tools/check_exports.py` and `python3 Design/Art/tools/catalogue.py --check`
- Container asset COPY/ignore rules: a scratch-stage export contains every catalogue URL and the
  contract, with no editable Blender files or previews

The cloud workspace used debug-free local build profiles to stay within its disk quota, and
`RUST_LOG=info` so process tests could read the server's startup message. The existing CPU
browser profile preserves CSS viewports and assertions; CI now selects it for GPU-free runners.
Blender 5.x compatibility is targeted but has not been executed here.

## Owner checkpoint before M9

Play solo with the wingman at both quarters tiers. Mark a relay, hold the other relay yourself,
return and dock; at tier 2, try a spare stock rig and a marked deposit. Assess whether scaled
threat and yield make runs trivial, or the wingman becomes a burden. Rework tunable values if
needed before M9. Automated checks do not establish that subjective balance verdict, nor do
NPC wingmen establish the human multiplayer gates.

The implementation raises the wire protocol to 14 and save documents to version 6. Earlier
save versions remain readable; the new optional stock reservation defaults to none.
