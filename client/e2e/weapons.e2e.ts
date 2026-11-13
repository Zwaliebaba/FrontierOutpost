import { expect, flyUntilLocked, launch, test } from "./fixtures";

test("holding Space damages the locked hostile", async ({ page }) => {
  test.setTimeout(60_000);
  await launch(page);
  await flyUntilLocked(page);
  const lock = page.locator("#lock");

  // The ore field's NPCs notice the pilot and close in; wait until one is in range.
  await expect(async () => {
    await page.keyboard.press("Tab");
    await expect(lock).toHaveAttribute("data-range", "in", { timeout: 500 });
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

test("a destroyed pilot sees what was lost and can take the next contract", async ({ page }) => {
  // The ore field's NPCs have to find and destroy an idle pilot, at tier I's quarter damage.
  test.setTimeout(180_000);
  await launch(page);
  // Toward the ore field, then idle among its NPCs.
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(12_000);
  await page.keyboard.up("KeyW");

  const summary = page.locator("#summary");
  await expect(summary).toHaveAttribute("data-ending", "destroyed", { timeout: 150_000 });
  await expect(summary).toContainText("Destroyed. Returned home");
  await expect(summary).toContainText("net −40 cr");

  await summary.getByRole("button", { name: "Next contract" }).click();
  await expect(summary).not.toHaveAttribute("data-ending");
  await page.locator('#briefing[data-state="open"] button[data-tier="1"]').click();
  await expect(page.locator("#run")).toHaveAttribute("data-phase", "atDock");
});
