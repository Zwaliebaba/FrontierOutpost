import { expect, newCallsign, signUp, test } from "./fixtures";

test("a new pilot registers a passkey and starts with 100 cr", async ({ page }) => {
  const callsign = await signUp(page);
  const account = page.locator("#account");
  await expect(account).toHaveAttribute("data-credits", "100");
  await expect(account).toContainText(callsign);
  await expect(page.locator("#briefing")).toHaveAttribute("data-state", "open");
});

test("a reload signs back in by token; a fresh tab signs in with the passkey", async ({ page }) => {
  const callsign = await signUp(page);
  const signInPanel = page.locator("#signin");

  // Same tab: the session token signs straight back in, with no passkey prompt.
  await page.reload();
  await expect(page.locator("#account")).toContainText(callsign);
  await expect(page.locator("#account")).toHaveAttribute("data-credits", "100");
  await expect(signInPanel).not.toHaveAttribute("data-state", "open");

  // The token forgotten, as in a new tab: the passkey signs in.
  await page.evaluate(() => {
    sessionStorage.clear();
  });
  await page.reload();
  await expect(signInPanel).toHaveAttribute("data-state", "open");
  await page.locator("#callsign").fill(callsign.toLowerCase());
  await page.locator('#signin button[data-action="sign-in"]').click();
  await expect(page.locator("#account")).toContainText(callsign);
  await expect(page.locator("#account")).toHaveAttribute("data-credits", "100");
});

test("launching pays for supplies from the pilot's credits", async ({ page }) => {
  await signUp(page);
  await page.locator('#briefing[data-state="open"] button[data-tier="1"]').click();
  await expect(page.locator("#account")).toHaveAttribute("data-credits", "60");
});

test("a taken callsign and an unknown one are refused with a reason", async ({ page }) => {
  const callsign = await signUp(page);
  await page.evaluate(() => {
    sessionStorage.clear();
  });
  await page.reload();
  const panel = page.locator("#signin");
  await page.locator("#callsign").fill(callsign);
  await page.locator('#signin button[data-action="register"]').click();
  await expect(panel).toContainText(`The callsign ${callsign} is taken.`);
  const nobody = newCallsign();
  await page.locator("#callsign").fill(nobody);
  await page.locator('#signin button[data-action="sign-in"]').click();
  await expect(panel).toContainText(`No pilot is called ${nobody}.`);
});
