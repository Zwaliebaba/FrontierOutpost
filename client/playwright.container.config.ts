import { defineConfig } from "@playwright/test";
import { shared } from "./playwright.config";

// The same tests against the servers running in containers (`docker compose -f compose.e2e.yaml
// up --build --wait` at the repository root), which serve the client they were built with.
// compose.e2e.yaml publishes the ports and settings the local run uses (playwright.config.ts):
// the compact site on seed 29 at 8181, and the server with no opening threat at 8182. Nothing is
// started here.
export default defineConfig(shared);
