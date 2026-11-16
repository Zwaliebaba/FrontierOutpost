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

/**
 * Waits for the launch to fly the ship out of the hangar and hand it over at the lane's end,
 * where the run counts it undocked: a jump is made from open space, and the pilot's keys do
 * nothing before then. The ship keeps the launch's speed, out along +x on the compact site.
 */
export async function leaveHangar(page: Page): Promise<void> {
  await page.locator('#run[data-phase="approach"]').waitFor({ timeout: 20_000 });
}

/** Out of the hangar, brakes until the ship is all but still. */
export async function leaveHangarAndStop(page: Page): Promise<void> {
  await leaveHangar(page);
  await page.keyboard.down("KeyS");
  await page.waitForTimeout(3_000);
  await page.keyboard.up("KeyS");
}

/**
 * From a ship stopped facing out along the lane, as `leaveHangarAndStop` leaves it, turns about
 * and flies straight back into the hangar mouth, which captures it and draws it in to dock.
 */
export async function flyBackIntoTheMouth(page: Page): Promise<void> {
  // 2.4 rad/s: half a turn in about 1.3 seconds.
  await page.keyboard.down("KeyA");
  await page.waitForTimeout(1_310);
  await page.keyboard.up("KeyA");
  await page.keyboard.down("KeyW");
  await page.locator("#summary[data-ending]").waitFor({ timeout: 20_000 });
  await page.keyboard.up("KeyW");
}

/**
 * Records every phase the run status shows, in order, on the page itself, so a phase as short
 * as the draw-in's second is not missed between polls. Read it with `phasesSeen`.
 */
export async function watchPhases(page: Page): Promise<void> {
  await page.evaluate(() => {
    const seen: string[] = [];
    const hangar: string[] = [];
    Object.assign(window, { phasesSeen: seen, hangarSeen: hangar });
    const run = document.querySelector<HTMLElement>("#run");
    const hud = document.querySelector<HTMLElement>("#hud");
    const record = () => {
      const phase = run?.dataset.phase;
      if (phase && seen.at(-1) !== phase) seen.push(phase);
      const inHangar = hud?.dataset.inHangar ?? "";
      if (hangar.at(-1) !== inHangar) hangar.push(inHangar);
    };
    for (const node of [run, hud]) {
      if (node) new MutationObserver(record).observe(node, { attributes: true });
    }
  });
}

/** The phases `watchPhases` saw, and each list of ships the hangar hid, in order. */
export async function phasesSeen(page: Page): Promise<{ phases: string[]; hangar: string[] }> {
  return page.evaluate(() => {
    const seen = window as unknown as { phasesSeen?: string[]; hangarSeen?: string[] };
    return { phases: seen.phasesSeen ?? [], hangar: seen.hangarSeen ?? [] };
  });
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
