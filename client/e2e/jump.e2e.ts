import { QUIET, expect, launch, position, test } from "./fixtures";

// On the quiet server no NPCs open the run, so nothing jams the drive.

test("clicking a marker and pressing J jumps to just outside that pocket", async ({ page }) => {
  test.setTimeout(60_000);
  await launch(page, QUIET);
  const run = page.locator("#run");
  const ore = page.locator('.nav-marker[data-target="oreField"]');

  await ore.click();
  await expect(ore).toHaveAttribute("data-selected", "true");
  await expect(run).toContainText("J jumps to Ore field");

  await page.keyboard.press("KeyJ");
  await expect(run).toContainText("jumping to Ore field in");
  await expect(ore).toHaveAttribute("data-jumping", "true");

  // The compact site's ore field: centre (45, -10), radius 36; the jump lands 40 m outside it.
  await expect(async () => {
    const { x, z } = await position(page);
    expect(Math.hypot(x - 45, z + 10)).toBeLessThan(80);
  }).toPass({ timeout: 15_000 });
  await expect(ore).toHaveAttribute("data-jumping", "false");
  await expect(run).not.toContainText("jumping to");
});

test("J explains itself with nothing chosen, and the server's refusal shows", async ({ page }) => {
  await launch(page, QUIET);
  const notice = page.locator("#notice");
  await page.keyboard.press("KeyJ");
  await expect(notice).toContainText("click the Dock or a pocket marker");

  await page.locator('.nav-marker[data-target="dock"]').click();
  await page.keyboard.press("KeyJ");
  await expect(notice).toHaveText("Jump drive: you are already there");
});

test("a spool is cancelled by pressing J again", async ({ page }) => {
  await launch(page, QUIET);
  await page.locator('.nav-marker[data-target="oreField"]').click({ button: "right" });
  await expect(page.locator("#run")).toContainText("jumping to Ore field in");
  await page.keyboard.press("KeyJ");
  await expect(page.locator("#notice")).toHaveText("Jump cancelled");
  await expect(page.locator("#run")).not.toContainText("jumping to");
});
