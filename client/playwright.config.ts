import { defineConfig } from "@playwright/test";

// End-to-end tests run the built client (`npm run build`) against the real game server.
const HTTP_PORT = 8181;

export default defineConfig({
  testDir: "e2e",
  testMatch: "**/*.e2e.ts",
  timeout: 30_000,
  reporter: "list",
  use: {
    baseURL: `http://127.0.0.1:${String(HTTP_PORT)}`,
    trace: "off",
    // A configured HTTP proxy cannot carry WebTransport, which runs over UDP.
    launchOptions: { args: ["--no-proxy-server"] },
  },
  projects: [{ name: "chromium", use: { browserName: "chromium" } }],
  webServer: {
    command: "cargo run --locked -p frontier-server",
    cwd: "..",
    url: `http://127.0.0.1:${String(HTTP_PORT)}/api/connect`,
    env: {
      FRONTIER_HTTP_ADDR: `127.0.0.1:${String(HTTP_PORT)}`,
      FRONTIER_WEBTRANSPORT_ADDR: "127.0.0.1:0",
      FRONTIER_CLIENT_DIR: "client/dist",
    },
    reuseExistingServer: false,
    // The first run compiles the server.
    timeout: 600_000,
  },
});
