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
