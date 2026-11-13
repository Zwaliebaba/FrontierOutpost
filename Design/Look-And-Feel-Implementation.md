# Look-and-feel review implementation

Implemented against main `baf5f0d` from the 13 November 2026 review. The changes retain Stage 1
rules, server authority, the 33° orthographic camera, authored ship proportions and all four
departure lanes. Protocol version 11 must be deployed with its matching client.

| Review task | Delivered change |
| --- | --- |
| T01 | Full map reserves target and gauge bands; notices keep clear of destination actions. |
| T02 | Actual launch equipment, capacity, supplies, exposure and missing fittings stay beside Launch; remedies and crew administration scroll. |
| T03 | Confirmed own jump restores own camera focus while retaining the hostile lock. |
| T04 | Flight readings use stable, bounded top-bar slots and wrap long values. |
| T05 | Service, fitting, crew, tier and launch controls have at least 44×44 CSS-pixel hit areas. |
| T06 | Full chart tracks its displayed size; numbered compact destinations retain full facts in the adjacent list and generous hit areas. |
| T07 | Map opening/closing manages focus; arrows, Home/End, Enter and F6 provide navigation while flight and combat keys remain live. |
| T08 | Hangar distinguishes saved plans from flown equipment and visibly names powered facility/tier prerequisites. |
| T09 | Crew shares validate whole percentages and total 100; draft, applying and server-applied states are distinct and survive account redraws. |
| T10 | Overview states built tier, running tier/OFF and effective capabilities. |
| T11 | Authored closed-socket cover and effective-tier service indicators preserve the owned module silhouette. |
| T12 | Home framing uses the measured panel gap and one maximum envelope; forward berth captions clear hulls. |
| T13 | Banked resources and available power identify affordable facilities, their benefit and the matching inspection card. |
| T14 | Server-published crew scaling separates shared pot and personal estimate; run-keyed pending receipts update to actual settlement, including final zero. Final receipts survive notice expiry and browser reload. |
| T15 | Scanned map explains both relay positions, 8 m range, simultaneous 30-second hold and conditional +50% contract bonus; selection shows range and progress. |
| T16 | Small, isolated material pulses follow authoritative hull loss or own shield loss; reduced motion disables them. |
| T17 | Reliable ship-scoped crew spool/cancel/end events drive bounded remote effects without enlarging snapshots. |
| T18 | Gear deployment checks each authored berth, rather than a radius around the apron centre. |
| T19 | Hostile forks gain restrained lit armor/recess details; beacon armor uses a small emissive cap with unchanged gameplay bounds. |
| T20 | Explicit AgX/exposure and bounded imported emissions retain warm windows; tactical markers preserve their signal colours. |

The processing text in the design and Stage 1 plan now agrees with the existing §10 rules and
implementation: server-clock batches continue online or away. The map focus contract above
preserves live movement and Tab-to-lock. These resolve the two decisions raised by the review.

## Verification

- Rust workspace tests, host clippy, formatting, WASM clippy and documentation warnings.
- Client formatting, lint, strict TypeScript, 317 unit tests and production build.
- Five new real-server browser regressions cover both primary viewport sizes, launch facts and
  hit areas, chart text/hits/focus, unavailable saved fitting, crew split acknowledgement and
  a two-pilot jump/camera handoff. Existing browser scenarios cover flight, economy, crews,
  reconnection and outpost management.
- The scan scenario checks that adding the next-wave reading preserves the phase position and
  displays enemy composition within the header at 1280×720, 1920×1080 and 1100×720.
- Blender 4.3.2 regeneration and manifest comparison preserve all four geometric berth and
  departure clearances. The three new cover meshes account for the obstacle-check count change
  from 434 to 437. Operating nodes stay outside static batches and have state tests.
- Receipt tests cover delayed settlement of an earlier run, final zero, unrelated messages,
  and restoring final history without stale pending state. Server tests cover real settlement
  and participation; the client never credits an account from its receipt cache.

The browser preview uses a granted local demonstration world. It demonstrates the layout and
roster state, not ordinary progression pacing. The affordability test also checks the ordinary
first-return balance of 160 cr and 10 banked ore against catalog costs. Pending receipts live
for the connected session; only completed receipts are cached across reloads, under the pilot
and origin, with a 32-entry limit. This cache is display history, not an authoritative ledger.

Software-rendered browser checks and source/geometry tests do not establish player comprehension
or owner-hardware performance. Use the existing playtest script for unfamiliar-player tasks,
grayscale/colour-vision review, relay coordination and maximum-load performance on the playtest
machines. No new hull family, audio system, bloom, shadows or Stage 2 features are introduced.
