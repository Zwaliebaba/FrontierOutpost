# syntax=docker/dockerfile:1
#
# The game server with the built client, as one Linux image.
#   docker compose up --build        then open http://localhost:8080/
# See README.md, "Running in a container".

# Keep these in step with rust-toolchain.toml, client/.nvmrc and the wasm-bindgen pin in Cargo.toml.
ARG RUST_VERSION=1.99.1
ARG NODE_VERSION=26
ARG DEBIAN_RELEASE=trixie
ARG WASM_BINDGEN_VERSION=0.2.130

FROM node:${NODE_VERSION}-${DEBIAN_RELEASE}-slim AS node

# One build stage with both toolchains, so the client builds with `npm run build` exactly as it
# does elsewhere: its prebuild step compiles frontier-sim to WebAssembly with cargo.
FROM rust:${RUST_VERSION}-${DEBIAN_RELEASE} AS build
ARG WASM_BINDGEN_VERSION
COPY --from=node /usr/local/bin/node /usr/local/bin/node
COPY --from=node /usr/local/lib/node_modules /usr/local/lib/node_modules
RUN ln -s ../lib/node_modules/npm/bin/npm-cli.js /usr/local/bin/npm \
    && ln -s ../lib/node_modules/npm/bin/npx-cli.js /usr/local/bin/npx

RUN --mount=type=cache,target=/usr/local/cargo/registry \
    cargo install wasm-bindgen-cli --version "${WASM_BINDGEN_VERSION}" --locked

WORKDIR /src
# Installs the components and the WebAssembly target the toolchain file names.
COPY rust-toolchain.toml ./
RUN rustup toolchain install

COPY client/package.json client/package-lock.json client/
RUN --mount=type=cache,target=/root/.npm \
    cd client && npm ci

COPY Cargo.toml Cargo.lock ./
COPY .cargo .cargo
COPY crates crates
COPY client client

# target/ is a cache mount, so the results are copied out of it within the same step.
RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/src/target \
    cargo build --locked --release -p frontier-server \
    && cd client && npm run build \
    && mkdir -p /out \
    && cp /src/target/release/frontier-server /out/ \
    && cp -r /src/client/dist /out/client

FROM debian:${DEBIAN_RELEASE}-slim
RUN useradd --uid 10001 --no-create-home --shell /usr/sbin/nologin frontier
COPY --from=build /out/frontier-server /usr/local/bin/frontier-server
COPY --from=build /out/client /srv/frontier/client

# Bind every interface inside the container. Browsers cannot dial 0.0.0.0, so set
# FRONTIER_PUBLIC_WEBTRANSPORT_ADDR to the address the host publishes UDP 4443 on (compose.yaml).
ENV FRONTIER_HTTP_ADDR=0.0.0.0:8080 \
    FRONTIER_WEBTRANSPORT_ADDR=0.0.0.0:4443 \
    FRONTIER_CLIENT_DIR=/srv/frontier/client
EXPOSE 8080/tcp 4443/udp

USER frontier
CMD ["frontier-server"]
