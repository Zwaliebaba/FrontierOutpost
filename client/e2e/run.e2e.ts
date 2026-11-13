import { QUIET, expect, flyIntoSite, lastNotice, launch, position, test } from "./fixtures";

// These run on the quiet server: no NPCs open the run, so nothing interrupts the pilot.

test("a pilot stops at a deposit, extracts ore and vents", async ({ page }) => {
  test.setTimeout(150_000);
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
  // In reach of the deposit, the extractor's key is prompted.
  await expect(page.locator('#prompts [data-prompt="extract"]')).toContainText("X");

  await page.keyboard.press("KeyX");
  await expect(run).toHaveAttribute("data-extractor", "on");
  // 2 ore a simulation minute. Allow wall-clock headroom when software rendering shares a CPU.
  await expect(run).toHaveAttribute("data-cargo", "1", { timeout: 60_000 });
  await expect(page.locator('#ship [data-gauge="hold"]')).toContainText("1/20");

  await page.keyboard.press("KeyV");
  await expect(run).toHaveAttribute("data-extractor", "venting");
  await expect(run).toHaveAttribute("data-extractor", "on", { timeout: 8_000 });
});

test("pressing X away from a deposit explains why nothing happened", async ({ page }) => {
  await launch(page, QUIET);
  await page.keyboard.press("KeyX");
  await expect(lastNotice(page)).toHaveText("Extractor: hold still beside a deposit");
});

test("pressing C out of range or on the move explains why nothing happened", async ({ page }) => {
  test.setTimeout(60_000);
  await launch(page, QUIET);
  const prompt = page.locator('#prompts [data-prompt="scan"]');
  // At the dock, 190 m from the site's centre, the scanner's 180 m do not reach: its key is
  // not among the prompts, and pressing it says why.
  await expect(page.locator('#prompts [data-prompt="cruise"]')).toBeVisible();
  await expect(prompt).toHaveCount(0);
  await page.keyboard.press("KeyC");
  await expect(lastNotice(page)).toHaveText(
    "Scan: out of range · move closer to the site's centre",
  );
  // Under way and in range, the key is prompted, and pressing it says what is missing.
  await page.keyboard.down("KeyW");
  await expect(async () => {
    expect((await position(page)).x).toBeGreaterThan(-100);
  }).toPass({ timeout: 30_000, intervals: [50] });
  await expect(prompt).toContainText("C");
  await page.keyboard.press("KeyC");
  await expect(lastNotice(page)).toHaveText("Scan: hold still first");
  await page.keyboard.up("KeyW");
  await expect(page.locator("#run")).toHaveAttribute("data-revealed", "false");
  await expect(page.locator('#run [data-cell="scan"]')).toHaveCount(0);
});

test("a scan held still in the site reveals the rich pocket and the waves", async ({ page }) => {
  test.setTimeout(90_000);
  await launch(page, QUIET);
  await flyIntoSite(page);
  await page.keyboard.down("KeyS");
  await page.waitForTimeout(2_000);
  await page.keyboard.up("KeyS");
  const run = page.locator("#run");
  const phaseBefore = await run.locator('[data-cell="phase"]').boundingBox();
  // Inside an unscanned site the scan's key is prompted; the scan then shows as a reading.
  await expect(page.locator('#prompts [data-prompt="scan"]')).toContainText("C");
  await page.keyboard.press("KeyC");
  await expect(run.locator('[data-cell="scan"]')).toContainText("hold still");
  await expect(run).toHaveAttribute("data-revealed", "true", { timeout: 20_000 });
  await expect(lastNotice(page)).toContainText("rich pocket: Derelict");
  await expect(run.locator('[data-cell="wave"]')).toBeVisible();
  await expect(run.locator('[data-cell="wave"]')).toContainText(/\d+ drones · \d+ raiders/);
  expect((await run.locator('[data-cell="phase"]').boundingBox())?.x).toBe(phaseBefore?.x);
  expect((await run.locator('[data-cell="phase"]').boundingBox())?.width).toBe(phaseBefore?.width);
  for (const size of [
    { width: 1280, height: 720 },
    { width: 1920, height: 1080 },
    { width: 1100, height: 720 },
  ]) {
    await page.setViewportSize(size);
    const clipped = await run.locator(".cell strong").evaluateAll((nodes) =>
      nodes
        .filter((node) => {
          const box = node.getBoundingClientRect();
          const header = document.querySelector("#command")?.getBoundingClientRect();
          return (
            parseFloat(getComputedStyle(node).fontSize) < 11 ||
            box.bottom > (header?.bottom ?? 0) ||
            node.scrollWidth > node.clientWidth + 1
          );
        })
        .map((node) => node.textContent),
    );
    expect(clipped).toEqual([]);
  }
  // The map tags what the scan found.
  await page.keyboard.press("KeyM");
  await expect(
    page.locator('.chart-row[data-target="derelict"] .tag[data-tag="rich"]'),
  ).toBeVisible();
});

test("flying out and back docks, shows the summary and offers the next contract", async ({
  page,
}) => {
  test.setTimeout(90_000);
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
  await expect(summary).toContainText("Your crew-pot share: 0 cr");
  // The contract was not met, so the supplies bought for this run are still aboard.
  await expect(summary.locator('[data-supplies="carried"]')).toContainText("next launch is free");
  await expect(page.locator("#account")).toHaveAttribute("data-credits", "60");

  // At home the client sends nothing; the server's keep-alive must hold the connection past
  // its 10-second idle timeout, which used to disconnect a pilot reading the summary.
  await page.waitForTimeout(12_000);
  await summary.getByRole("button", { name: "Next contract" }).click();
  await expect(page.locator("#briefing")).toHaveAttribute("data-state", "open");
  await expect(page.locator("#briefing .stake")).toContainText("Supplies: carried over");
  await page.locator('#briefing button[data-tier="1"]').click();
  await expect(page.locator("#run")).toHaveAttribute("data-phase", "atDock");
  await expect(page.locator("#status")).toHaveAttribute("data-state", "connected");
  // The relaunch cost nothing.
  await expect(page.locator("#account")).toHaveAttribute("data-credits", "60");
});

test("holding Shift engages the cruise drive on the legs", async ({ page }) => {
  await launch(page, QUIET);
  // On the leg the drives read ready, and the cruise drive's key is prompted.
  await expect(page.locator('#run [data-cell="drive"]')).toContainText("Ready");
  await expect(page.locator('#prompts [data-prompt="cruise"]')).toContainText("Shift");
  const start = await position(page);
  await page.keyboard.down("ShiftLeft");
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(2_000);
  const cruised = await position(page);
  await page.keyboard.up("KeyW");
  await page.keyboard.up("ShiftLeft");
  // Two seconds from rest: about 10 m at normal speed, three times the acceleration on the drive.
  expect(cruised.x - start.x).toBeGreaterThan(20);
  // Prediction ran the drive too, so the server barely corrected it.
  const correction = Number(
    /correction (\d+) cm/.exec((await page.textContent("#hud")) ?? "")?.[1],
  );
  expect(correction).toBeLessThan(150);
});

test("without Shift the ship keeps normal speed", async ({ page }) => {
  await launch(page, QUIET);
  const start = await position(page);
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(2_000);
  const flown = await position(page);
  await page.keyboard.up("KeyW");
  expect(flown.x - start.x).toBeLessThan(15);
});
