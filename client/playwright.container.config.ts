import { defineConfig } from "@playwright/test";
import { shared } from "./playwright.config";

// The same tests against the server running in a container (`docker compose up --build` at the
// repository root), which serves the client it was built with. Nothing is started here.
//
// The container offers the real site with a random seed, as a player sees it. The tests below
// need what the local servers are started with instead (playwright.config.ts): the compact
// site's 20-second approach, seed 29's clear line to a deposit, and a second server with no
// opening threat. So only the tests that hold on any site run here.
export default defineConfig({
  ...shared,
  use: { ...shared.use, baseURL: "http://127.0.0.1:8080" },
  testIgnore: ["**/lock.e2e.ts", "**/weapons.e2e.ts", "**/run.e2e.ts"],
});
