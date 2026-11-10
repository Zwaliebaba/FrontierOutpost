import { defineConfig } from "@playwright/test";
import { shared } from "./playwright.config";

// The same tests against the server running in a container (`docker compose up --build` at the
// repository root), which serves the client it was built with. Nothing is started here.
export default defineConfig({
  ...shared,
  use: { ...shared.use, baseURL: "http://127.0.0.1:8080" },
});
