import { expect, launch, position, signUp, test } from "./fixtures";

test("the briefing shows the contract and opens only tier I", async ({ page }) => {
  await signUp(page);
  const briefing = page.locator("#briefing");
  await expect(briefing).toHaveAttribute("data-state", "open");
  await expect(briefing).toContainText("Ore field: open");
  await expect(briefing).toContainText("Derelict: locked, needs a sensor array");
  await expect(briefing.locator('button[data-tier="1"]')).toBeEnabled();
  await expect(briefing.locator('button[data-tier="2"]')).toBeDisabled();
});

test("the player's ship leaves the dock when they hold thrust", async ({ page }) => {
  await launch(page);
  await expect(page.locator("#briefing")).not.toHaveAttribute("data-state", "open");
  await expect(page.locator("#run")).toHaveAttribute("data-phase", "atDock");
  const start = await position(page);
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(1_500);
  await page.keyboard.up("KeyW");
  await page.waitForTimeout(300);
  const end = await position(page);
  expect(Math.hypot(end.x - start.x, end.z - start.z)).toBeGreaterThan(2);
  // The dock and the pockets are labelled, on screen or at its edge.
  await expect(page.locator('.nav-marker[data-target="oreField"]')).toContainText("Ore field");
  await expect(page.locator('.nav-marker[data-target="derelict"]')).toContainText("(locked)");
});
