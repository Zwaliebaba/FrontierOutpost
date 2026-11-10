import { expect, test } from "@playwright/test";

test("the client connects to the real server and completes the handshake", async ({ page }) => {
  await page.goto("/");
  const status = page.locator("#status");
  await expect(status).toHaveAttribute("data-state", "connected");
  await expect(status).toHaveText("Connected · protocol 1");
});
