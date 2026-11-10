import { defineConfig } from "@playwright/test";

// End-to-end tests run the built client (`npm run build`) against the real game server, on the
// compact site with a fixed seed: a clear line from the dock to an ore deposit at (64, 1).
const HTTP_PORT = 8181;
/** A second server whose runs open with no NPCs, for extraction and docking. */
const QUIET_PORT = 8182;
const SEED = "29";

const server = (port: number, extra: Record<string, string>) => ({
  command: "cargo run --locked -p frontier-server",
  cwd: "..",
  url: `http://127.0.0.1:${String(port)}/api/connect`,
  env: {
    FRONTIER_HTTP_ADDR: `127.0.0.1:${String(port)}`,
    FRONTIER_WEBTRANSPORT_ADDR: "127.0.0.1:0",
    FRONTIER_CLIENT_DIR: "client/dist",
    FRONTIER_SITE: "compact",
    FRONTIER_SEED: SEED,
    ...extra,
  },
  reuseExistingServer: false,
  // The first run compiles the server.
  timeout: 600_000,
});

/** Settings for every run, whichever server the tests talk to. */
export const shared = defineConfig({
  testDir: "e2e",
  testMatch: "**/*.e2e.ts",
  timeout: 30_000,
  // Each launch is its own instance, so tests do not share NPCs; one worker keeps the load of
  // software-rendered pages predictable.
  workers: 1,
  reporter: "list",
  use: {
    baseURL: `http://127.0.0.1:${String(HTTP_PORT)}`,
    trace: "off",
    // A configured HTTP proxy cannot carry WebTransport, which runs over UDP.
    launchOptions: { args: ["--no-proxy-server"] },
  },
  projects: [{ name: "chromium", use: { browserName: "chromium" } }],
});

export default defineConfig({
  ...shared,
  webServer: [server(HTTP_PORT, {}), server(QUIET_PORT, { FRONTIER_NPCS: "0" })],
});
