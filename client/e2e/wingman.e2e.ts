import { RICH, expect, signUp, test } from "./fixtures";

test("powered quarters hire an allied wingman and accept each tier-one order", async ({ page }) => {
  test.setTimeout(90_000);
  await signUp(page, RICH);
  const home = page.locator('#home[data-state="open"]');
  await home.locator('button[data-action="view:facilities"]').click();
  await home.locator('button[data-action="install:crew_quarters"]').click();
  await expect(page.locator("#account")).toHaveAttribute("data-credits", "950");
  await page.locator("#wingman-hire").check();
  await page.locator('#briefing button[data-tier="1"]').click();
  await page.locator("#hud[data-x]").waitFor();
  const wing = page.locator("#wingman-panel");
  await expect(wing).toBeVisible();
  await expect(wing).toHaveAttribute("data-order", "follow");
  await expect(page.locator("#account")).toHaveAttribute("data-credits", "870");
  await page.keyboard.press("KeyH");
  const canvas = page.locator("canvas").first();
  const box = await canvas.boundingBox();
  if (!box) throw new Error("flight canvas missing");
  await page.mouse.click(box.x + box.width * 0.55, box.y + box.height * 0.45);
  await expect(wing).toHaveAttribute("data-order", "hold");
  await page.keyboard.press("KeyF");
  await expect(wing).toHaveAttribute("data-order", "follow");
  await page.keyboard.press("KeyB");
  await expect(wing).toHaveAttribute("data-order", "return");
  // Home is through the mouth: the launch flies the wingman out behind its pilot, to the lane's
  // end, and from there it turns back and flies into the mouth, which draws it in.
  await expect(wing).toHaveAttribute("data-state", "docked", { timeout: 40_000 });
});
