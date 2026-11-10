import { expect, test } from "@playwright/test";

test("the player's ship moves when they hold thrust", async ({ page }) => {
  await page.goto("/");
  const hud = page.locator("#hud");
  await expect(hud).toHaveAttribute("data-x", /.+/);
  const start = {
    x: Number(await hud.getAttribute("data-x")),
    z: Number(await hud.getAttribute("data-z")),
  };

  await page.keyboard.down("KeyW");
  await page.waitForTimeout(1_500);
  await page.keyboard.up("KeyW");
  await page.waitForTimeout(300);

  const end = {
    x: Number(await hud.getAttribute("data-x")),
    z: Number(await hud.getAttribute("data-z")),
  };
  expect(Math.hypot(end.x - start.x, end.z - start.z)).toBeGreaterThan(2);
});

test("two players in one expedition see each other's ships", async ({ browser }) => {
  const first = await browser.newPage();
  const second = await browser.newPage();
  await first.goto("/");
  await second.goto("/");
  const shipOf = async (page: typeof first) => {
    await expect(page.locator("#hud")).toHaveAttribute("data-ship", /\d+/);
    return (await page.locator("#hud").getAttribute("data-ship")) ?? "";
  };
  const [firstShip, secondShip] = [await shipOf(first), await shipOf(second)];
  expect(firstShip).not.toBe(secondShip);
  const sees = (ship: string) => new RegExp(`(^| )${ship}( |$)`);
  await expect(first.locator("#hud")).toHaveAttribute("data-ships", sees(secondShip));
  await expect(second.locator("#hud")).toHaveAttribute("data-ships", sees(firstShip));
  await first.close();
  await second.close();
});
