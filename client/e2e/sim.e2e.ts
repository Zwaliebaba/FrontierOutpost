import { expect, test } from "./fixtures";

test("the client runs the Rust simulation compiled to WebAssembly", async ({ page }) => {
  await page.goto("/");
  const sim = page.locator("#sim");
  // The number comes from frontier_sim::TICKS_PER_SECOND, called through WebAssembly.
  await expect(sim).toHaveAttribute("data-state", "loaded");
  await expect(sim).toHaveText(/^Simulation · 20 ticks per second/);
});
