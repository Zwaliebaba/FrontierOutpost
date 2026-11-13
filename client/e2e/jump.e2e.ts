import { QUIET, expect, launch, position, test } from "./fixtures";

// On the quiet server no NPCs open the run, so nothing jams the drive.

test("choosing a destination on the map and pressing J jumps to just outside it", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await launch(page, QUIET);
  const map = page.locator("#chart");
  const jump = page.locator("#jump");
  const ore = page.locator('.chart-row[data-target="oreField"]');
  await expect(jump).toHaveAttribute("data-state", "idle");

  await page.keyboard.press("KeyM");
  await expect(map).toHaveAttribute("data-state", "open");
  await ore.click();
  await expect(ore).toHaveAttribute("data-selected", "true");
  await expect(jump).toHaveAttribute("data-state", "chosen");
  await expect(jump).toContainText("Ore field");
  // The scene's one label: the waypoint to the destination chosen.
  await expect(page.locator('#waypoint[data-target="oreField"]')).toContainText("Ore field");

  await page.keyboard.press("KeyJ");
  await expect(jump).toContainText("Jumping to Ore field in");
  // The map closes as the drive spools, so the pilot sees a hostile coming.
  await expect(map).not.toHaveAttribute("data-state", "open");

  // The compact site's ore field: centre (45, -10), radius 36; the jump lands 40 m outside it.
  await expect(async () => {
    const { x, z } = await position(page);
    expect(Math.hypot(x - 45, z + 10)).toBeLessThan(80);
  }).toPass({ timeout: 15_000 });
  await expect(jump).toHaveAttribute("data-state", "chosen");
  await expect(jump).not.toContainText("Jumping to");
});

test("J with nothing chosen opens the map, and the server's refusal shows", async ({ page }) => {
  await launch(page, QUIET);
  const notice = page.locator("#notice");
  await page.keyboard.press("KeyJ");
  await expect(notice).toContainText("choose a destination on the map");
  await expect(page.locator("#chart")).toHaveAttribute("data-state", "open");

  // A digit chooses the destination it is listed beside: 1 is the dock.
  await page.keyboard.press("Digit1");
  await expect(page.locator('.chart-row[data-target="dock"]')).toHaveAttribute(
    "data-selected",
    "true",
  );
  await page.keyboard.press("KeyJ");
  await expect(notice).toHaveText("Jump drive: you are already there");
  // Refused, so the map stays open for another choice.
  await expect(page.locator("#chart")).toHaveAttribute("data-state", "open");
});

test("a spool is cancelled by pressing J again", async ({ page }) => {
  await launch(page, QUIET);
  const jump = page.locator("#jump");
  await page.keyboard.press("KeyM");
  // A double click chooses and engages at once.
  await page.locator('.chart-row[data-target="oreField"]').dblclick();
  await expect(jump).toContainText("Jumping to Ore field in");
  await page.keyboard.press("KeyJ");
  await expect(page.locator("#notice")).toHaveText("Jump cancelled");
  await expect(jump).not.toContainText("Jumping to");
});

test("the map says why a locked pocket cannot be jumped to, and its button jumps", async ({
  page,
}) => {
  await launch(page, QUIET);
  const map = page.locator("#chart");
  const engage = map.locator('button[data-action="engage-jump"]');
  // The minimap opens the map for a pilot without a keyboard.
  await page.locator("#minimap").click();
  await expect(map).toHaveAttribute("data-state", "open");
  await expect(engage).toBeDisabled();
  await expect(map.locator(".action-note")).toHaveText("Choose a destination");

  await map.locator('.chart-row[data-target="derelict"]').click();
  await expect(engage).toBeDisabled();
  await expect(map.locator(".action-note")).toHaveText("Locked: needs a sensor array");

  await map.locator('.chart-row[data-target="oreField"]').click();
  await expect(engage).toBeEnabled();
  await engage.click();
  await expect(page.locator("#jump")).toHaveAttribute("data-state", "spooling");
  await expect(map).not.toHaveAttribute("data-state", "open");
});

test("Esc closes the map and leaves the choice", async ({ page }) => {
  await launch(page, QUIET);
  const map = page.locator("#chart");
  await page.keyboard.press("KeyM");
  await page.keyboard.press("Digit2");
  await page.keyboard.press("Escape");
  await expect(map).not.toHaveAttribute("data-state", "open");
  await expect(page.locator("#jump")).toHaveAttribute("data-target", "oreField");
  // Space is the trigger: it never presses the map's buttons, open or closed.
  await page.keyboard.press("KeyM");
  await map.locator('.chart-row[data-target="oreField"]').focus();
  await page.keyboard.press("Space");
  await expect(page.locator("#jump")).toHaveAttribute("data-target", "oreField");
});
