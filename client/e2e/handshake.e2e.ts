import { expect, test } from "@playwright/test";
import { PROTOCOL_VERSION } from "../src/protocol";

test("the client connects to the real server and completes the handshake", async ({ page }) => {
  await page.goto("/");
  const status = page.locator("#status");
  await expect(status).toHaveAttribute("data-state", "connected");
  await expect(status).toHaveText(`Connected · protocol ${String(PROTOCOL_VERSION)}`);
});
