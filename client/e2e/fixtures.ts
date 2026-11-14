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

/**
 * Gives a page a virtual authenticator that verifies its user, as a laptop's platform
 * authenticator does, so passkeys can be made and used without a person.
 */
export async function addAuthenticator(page: Page): Promise<void> {
  const cdp = await page.context().newCDPSession(page);
  await cdp.send("WebAuthn.enable");
  await cdp.send("WebAuthn.addVirtualAuthenticator", {
    options: {
      protocol: "ctap2",
      transport: "internal",
      hasResidentKey: true,
      hasUserVerification: true,
      isUserVerified: true,
      automaticPresenceSimulation: true,
    },
  });
}

/** Playwright's `test`, with a passkey authenticator on each page and the page left like a
 * closing tab. */
export const test = base.extend({
  page: async ({ page }, use) => {
    await addAuthenticator(page);
    await use(page);
    if (!page.isClosed()) await leave(page);
  },
});

export { expect } from "@playwright/test";

/** The server whose runs open with no NPCs (playwright.config.ts). Passkeys need a domain
 * name, so pages are opened at localhost, never 127.0.0.1. */
export const QUIET = "http://localhost:8182/";

/** The server whose new pilots start with 1,100 cr, 40 ore, 30 alloy and 10 components
 * (playwright.config.ts), enough to build. */
export const RICH = "http://localhost:8183/";

let pilots = 0;

/** A callsign no other test has used against this server. */
export function newCallsign(): string {
  pilots += 1;
  return `T${Date.now().toString(36).slice(-6)}${String(pilots)}`;
}

/** Opens the game and registers a new pilot with a passkey. Returns their callsign. */
export async function signUp(page: Page, url = "/"): Promise<string> {
  await page.goto(url);
  const callsign = newCallsign();
  await page.locator("#callsign").fill(callsign);
  await page.locator('#signin button[data-action="register"]').click();
  await page.locator("#account[data-credits]").waitFor();
  return callsign;
}

/** Signs up a new pilot, takes the contract at tier I and waits to be flying. */
export async function launch(page: Page, url = "/"): Promise<void> {
  await signUp(page, url);
  await page.locator('#briefing[data-state="open"] button[data-tier="1"]').click();
  await page.locator("#hud[data-x]").waitFor();
}

/** Flies off the berth until the run counts the ship undocked: a jump is made from open space. */
export async function flyClear(page: Page): Promise<void> {
  await page.keyboard.down("KeyW");
  await page.locator('#run[data-phase="approach"]').waitFor({ timeout: 20_000 });
  await page.keyboard.up("KeyW");
}

/** Flies straight out of the dock until the ship is inside the ore field. */
export async function flyIntoSite(page: Page): Promise<void> {
  await page.keyboard.down("KeyW");
  await page.locator('#run[data-phase="objective"]').waitFor({ timeout: 45_000 });
  await page.keyboard.up("KeyW");
}

/** The newest notice on show. Earlier ones stay beside it until their own time is up. */
export function lastNotice(page: Page) {
  return page.locator("#notice .notice").last();
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
