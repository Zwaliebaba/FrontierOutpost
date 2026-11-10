import { QUIET, expect, flyIntoSite, launch, position, test } from "./fixtures";

// These run on the quiet server: no NPCs open the run, so nothing interrupts the pilot.

test("a pilot stops at a deposit, extracts ore and vents", async ({ page }) => {
  test.setTimeout(90_000);
  await launch(page, QUIET);
  const run = page.locator("#run");
  // Straight out of the dock, then brake beside the deposit at (64, 1).
  await page.keyboard.down("KeyW");
  await expect(async () => {
    expect((await position(page)).x).toBeGreaterThan(57);
  }).toPass({ timeout: 40_000, intervals: [50] });
  await page.keyboard.up("KeyW");
  await page.keyboard.down("KeyS");
  await page.waitForTimeout(1_500);
  await page.keyboard.up("KeyS");
  await expect(run).toContainText("(in reach)");

  await page.keyboard.press("KeyX");
  await expect(run).toHaveAttribute("data-extractor", "on");
  // 2 ore a minute: the first unit within 30 seconds.
  await expect(run).toHaveAttribute("data-cargo", "1", { timeout: 35_000 });
  await expect(run).toContainText("hold 1/20");

  await page.keyboard.press("KeyV");
  await expect(run).toHaveAttribute("data-extractor", "venting");
  await expect(run).toHaveAttribute("data-extractor", "on", { timeout: 8_000 });
});

test("pressing X away from a deposit explains why nothing happened", async ({ page }) => {
  await launch(page, QUIET);
  await page.keyboard.press("KeyX");
  await expect(page.locator("#notice")).toHaveText("Extractor: hold still beside a deposit");
});

test("a scan held still in the site reveals the rich pocket and the waves", async ({ page }) => {
  test.setTimeout(60_000);
  await launch(page, QUIET);
  await flyIntoSite(page);
  await page.keyboard.down("KeyS");
  await page.waitForTimeout(2_000);
  await page.keyboard.up("KeyS");
  const scan = page.locator("#scan");
  await page.keyboard.press("KeyC");
  await expect(scan).toContainText("Scanning");
  await expect(scan).toHaveAttribute("data-revealed", "true", { timeout: 20_000 });
  await expect(scan).toContainText("rich: Derelict");
  await expect(page.locator('.nav-marker[data-target="derelict"]')).toContainText("(rich)");
});

test("flying out and back docks, shows the summary and offers the next contract", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await launch(page, QUIET);
  // Out past the undock radius, stop, turn about, and fly straight back.
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(5_000);
  await page.keyboard.up("KeyW");
  await expect(page.locator("#run")).toHaveAttribute("data-phase", "approach");
  await page.keyboard.down("KeyS");
  await page.waitForTimeout(3_000);
  await page.keyboard.up("KeyS");
  // 2.4 rad/s: half a turn in about 1.3 seconds.
  await page.keyboard.down("KeyA");
  await page.waitForTimeout(1_310);
  await page.keyboard.up("KeyA");
  await page.keyboard.down("KeyW");
  const summary = page.locator("#summary");
  await expect(summary).toHaveAttribute("data-ending", "docked", { timeout: 20_000 });
  await page.keyboard.up("KeyW");
  await expect(summary).toContainText("Banked: nothing");
  await expect(summary).toContainText("Contract: 0 cr");

  await summary.getByRole("button", { name: "Next contract" }).click();
  await expect(page.locator("#briefing")).toHaveAttribute("data-state", "open");
});
