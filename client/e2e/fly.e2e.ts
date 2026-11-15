import {
  QUIET,
  expect,
  launch,
  leaveHangar,
  leaveHangarAndStop,
  position,
  signUp,
  test,
} from "./fixtures";

test("flight input continues when drawing is paused", async ({ page }) => {
  await launch(page, QUIET);
  // Stopped out of the hangar, so only the pilot's thrust can move the ship.
  await leaveHangarAndStop(page);
  const start = await position(page);
  await page.evaluate(() => {
    window.requestAnimationFrame = () => 0;
  });
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(2_000);
  await page.keyboard.up("KeyW");
  // Rejoin to read the authoritative position; the paused renderer cannot update its HUD.
  await page.reload();
  await page.locator("#hud[data-x]").waitFor();
  expect((await position(page)).x - start.x).toBeGreaterThan(5);
});

test("the briefing shows the contract and opens only tier I", async ({ page }) => {
  await signUp(page);
  const briefing = page.locator("#briefing");
  await expect(briefing).toHaveAttribute("data-state", "open");
  await expect(briefing).toContainText("Ore field: open");
  await expect(briefing).toContainText("Derelict: locked, needs a sensor array");
  await expect(briefing.locator('button[data-tier="1"]')).toBeEnabled();
  await expect(briefing.locator('button[data-tier="2"]')).toBeDisabled();
});

test("the launch flies the ship out of the hangar, then the pilot's thrust moves it", async ({
  page,
}) => {
  await launch(page);
  await expect(page.locator("#briefing")).not.toHaveAttribute("data-state", "open");
  await expect(page.locator("#run")).toHaveAttribute("data-phase", "atDock");
  await expect(page.locator('#prompts [data-prompt="departing"]')).toContainText(
    "Leaving the hangar",
  );
  // With no key pressed the launch flies the ship out along the lane, +x on the compact site.
  const start = await position(page);
  await leaveHangar(page);
  const out = await position(page);
  expect(out.x - start.x).toBeGreaterThan(20);
  expect(Math.abs(out.z)).toBeLessThan(0.5);
  // At the lane's end the ship is the pilot's: turned and thrust, it leaves the lane's line.
  // 2.4 rad/s: a quarter turn in about 0.65 s.
  await page.keyboard.down("KeyA");
  await page.waitForTimeout(650);
  await page.keyboard.up("KeyA");
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(1_000);
  await page.keyboard.up("KeyW");
  await page.waitForTimeout(300);
  const end = await position(page);
  expect(Math.abs(end.z - out.z)).toBeGreaterThan(2);
  // The scene carries no labels: the map lists the dock and the pockets, and how far each is.
  await expect(page.locator("#waypoint")).toBeHidden();
  await page.keyboard.press("KeyM");
  await expect(page.locator('.chart-row[data-target="oreField"]')).toContainText("Ore field");
  await expect(page.locator('.chart-row[data-target="oreField"] .row-distance')).toHaveText(
    /\d+ m/,
  );
  await expect(page.locator('.chart-row[data-target="derelict"]')).toContainText("Locked");
});
