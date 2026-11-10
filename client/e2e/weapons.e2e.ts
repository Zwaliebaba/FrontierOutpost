import { expect, test } from "./fixtures";

test("holding Space damages the locked hostile", async ({ page }) => {
  await page.goto("/");
  await expect(page.locator("#hud")).toHaveAttribute("data-x", /.+/);
  const lock = page.locator("#lock");

  // The skirmish's drones notice the pilot and close in; wait until one is in range.
  await expect(async () => {
    await page.keyboard.press("Tab");
    await expect(lock).toContainText("in range", { timeout: 500 });
  }).toPass({ timeout: 15_000 });
  const target = await lock.getAttribute("data-target");

  await page.keyboard.down("Space");
  // Damaged, or destroyed outright, which releases the lock.
  await expect(async () => {
    const now = await lock.getAttribute("data-target");
    const text = (await lock.textContent()) ?? "";
    const hull = /hull (\d+)%/.exec(text)?.[1];
    expect(now !== target || (hull !== undefined && Number(hull) < 100)).toBe(true);
  }).toPass({ timeout: 6_000 });
  await page.keyboard.up("Space");
});
