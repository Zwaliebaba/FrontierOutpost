# Frontier Outpost

Establish and enhance your outpost. Equip ships for dangerous expeditions, bring discoveries home,
and expand the facilities that determine what you can build and where you can go.

Frontier Outpost is a multiplayer isometric space game for solo players and crews of 1–4. It has an
authoritative Rust server and an HTML5 client built with TypeScript and Three.js.

![The outpost scene](Design/FrontierOutpost/FrontierOutpostPreview.png)

## Status

Pre-production. The repository holds the game design, the visual style guide, a reference Blender
scene, and a minimal code skeleton with CI. No gameplay is implemented yet.

## Documents

- [Game design](Design/FrontierOutpost.md)
- [Stage 1 implementation plan](Design/Stage1-Plan.md)
- [Visual style guide](Design/Visual-Style-Guide.md)
- [Outpost scene](Design/FrontierOutpost/README.md)
- [Contributor and agent guide](AGENTS.md): layout, commands, architecture rules, code standards

## Getting started

Prerequisites: [rustup](https://rustup.rs/) (it installs the pinned toolchain and the
WebAssembly target), the wasm-bindgen tool at the version pinned in `Cargo.toml`
(`cargo install wasm-bindgen-cli --version 0.2.130 --locked`), Node.js 26 (see `client/.nvmrc`),
and Blender 4.3+ for scene work.

```sh
git clone https://github.com/Zwaliebaba/FrontierOutpost.git
cd FrontierOutpost

cargo test --workspace
cargo run -p frontier-server

cd client
npm ci
npm run dev
```

The home screen uses Station Command service views around a four-berth apron. Crew ships use
the same authored slots at home and when launching. See the [station upgrade notes](Design/FrontierOutpost/README.md#four-berth-station-command-upgrade).

After building the client, `npm run e2e` runs browser checks against the real server. On a
CPU-only machine, `npm run e2e:software` uses the same tests and CSS viewport with half-density
rendering buffers. The ordinary configuration keeps full-density rendering.

## Trying the game under latency

The server can route WebTransport through a relay that delays and drops packets, so a local
game feels like a distant one. Each setting applies in each direction, so a one-way delay of
75 ms gives about 150 ms round trip.

```sh
cd client && npm run build && cd ..
FRONTIER_IMPAIR_DELAY_MS=75 FRONTIER_CLIENT_DIR=client/dist cargo run -p frontier-server
```

Optional: `FRONTIER_IMPAIR_JITTER_MS` adds up to that much random extra delay per packet, and
`FRONTIER_IMPAIR_LOSS_PERCENT` drops that share of packets. Open the printed address. The status
line shows the measured round trip, which includes up to one 50 ms server tick, and the last
prediction correction.

## Running in a container

The server and the built client also run as one Linux image, for example under Docker Desktop.
Nothing else needs installing; the image builds both.

```sh
docker compose up --build
```

Open <http://localhost:8080/>, not `127.0.0.1`: passkeys need a domain name. `docker compose
down` stops it; the world's saves stay in the `saves` volume until `docker compose down -v`.
To start an empty world, stop the server and run `docker compose run --rm server frontier-server
wipe`. In VS Code, the **Game in container
(Edge)** launch configuration does all of this in one step: it starts the container, opens the
game in Edge with the debugger attached, and stops the container when Edge closes.

The end-to-end tests need servers set up for them: the compact site on a fixed seed, and a second
server whose runs open with no NPCs. [compose.e2e.yaml](compose.e2e.yaml) runs both, beside the
game if it is up. Run `npx playwright install chromium` once in `client/`, then:

```sh
docker compose -f compose.e2e.yaml up --build --wait
cd client && npm run e2e:container
docker compose -f compose.e2e.yaml down
```

[compose.yaml](compose.yaml) publishes HTTP on TCP 8080 and WebTransport on **UDP** 4443, both on
loopback only, because WebTransport needs a secure context and plain HTTP is one only on
localhost. Inside the container the server binds `0.0.0.0`, which a browser cannot dial, so
`FRONTIER_PUBLIC_WEBTRANSPORT_ADDR` gives the address browsers should use instead: the host side
of the UDP mapping. Change it together with the port mapping.

The latency relay does not work in a container: it listens on a port it picks at start, which
cannot be published in advance, so the server refuses `FRONTIER_IMPAIR_*` together with a public
address. The server makes a new certificate each time it starts, valid for 13 days. Restart a
container that has been up longer than that.

## Licence

All rights reserved. See [LICENSE](LICENSE).
