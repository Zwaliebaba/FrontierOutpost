import { expect, test } from "./fixtures";

test("Tab locks the nearest hostile and Escape releases it", async ({ page }) => {
  await page.goto("/");
  await expect(page.locator("#hud")).toHaveAttribute("data-x", /.+/);
  const lock = page.locator("#lock");

  await page.keyboard.press("Tab");
  await expect(lock).toHaveAttribute("data-target", /\d+/);
  await expect(lock).toContainText(/Locked: (Drone|Raider|Warden) \d+/);
  const first = await lock.getAttribute("data-target");

  await page.keyboard.press("Tab");
  await expect(lock).not.toHaveAttribute("data-target", first ?? "");

  await page.keyboard.press("Escape");
  await expect(lock).toBeEmpty();
});
