import { test as base } from "@playwright/test";
import type { Page } from "@playwright/test";

/**
 * Closes a page the way a closing tab does. Playwright closes pages without firing `pagehide`,
 * so the game would never close its session, and the server would keep a ghost ship in the
 * four-pilot expedition until the session timed out.
 */
export async function leave(page: Page): Promise<void> {
  await page.evaluate(() => {
    window.dispatchEvent(new Event("pagehide"));
  });
  // Give the close a moment to reach the server before the page goes.
  await page.waitForTimeout(150);
  await page.close();
}

/** Playwright's `test`, with each test's page left like a closing tab. */
export const test = base.extend({
  page: async ({ page }, use) => {
    await use(page);
    if (!page.isClosed()) await leave(page);
  },
});

export { expect } from "@playwright/test";

/** The server whose runs open with no NPCs (playwright.config.ts). */
export const QUIET = "http://127.0.0.1:8182/";

/** Opens the game, takes the contract at tier I and waits to be flying. */
export async function launch(page: Page, url = "/"): Promise<void> {
  await page.goto(url);
  await page.locator('#briefing[data-state="open"] button[data-tier="1"]').click();
  await page.locator("#hud[data-x]").waitFor();
}

/** Flies straight out of the dock until the ship is inside the ore field. */
export async function flyIntoSite(page: Page): Promise<void> {
  await page.keyboard.down("KeyW");
  await page.locator('#run[data-phase="objective"]').waitFor({ timeout: 30_000 });
  await page.keyboard.up("KeyW");
}

/** The ship's position from the HUD. */
export async function position(page: Page): Promise<{ x: number; z: number }> {
  const hud = page.locator("#hud");
  return {
    x: Number(await hud.getAttribute("data-x")),
    z: Number(await hud.getAttribute("data-z")),
  };
}

/**
 * Flies toward the site until Tab locks a hostile. The ore field's NPCs come out to meet the
 * pilot, so where that happens varies.
 */
export async function flyUntilLocked(page: Page): Promise<void> {
  await page.keyboard.down("KeyW");
  const lock = page.locator("#lock[data-target]");
  for (let tries = 0; tries < 80 && !(await lock.count()); tries += 1) {
    await page.keyboard.press("Tab");
    await page.waitForTimeout(400);
  }
  await page.keyboard.up("KeyW");
  await lock.waitFor({ timeout: 1_000 });
}
