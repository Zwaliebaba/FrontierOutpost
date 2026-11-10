import { QUIET, addAuthenticator, expect, leave, signUp, test } from "./fixtures";

test("two pilots crew up by code, and the wing follows the host into one run", async ({ page }) => {
  const wing = await page.context().newPage();
  await addAuthenticator(wing);
  const hostName = await signUp(page, QUIET);
  const wingName = await signUp(wing, QUIET);

  const hostBriefing = page.locator('#briefing[data-state="open"]');
  await hostBriefing.locator('button[data-action="create-crew"]').click();
  const code = await hostBriefing.locator(".crew[data-code]").getAttribute("data-code");
  expect(code).toMatch(/^[A-Z2-9]{6}$/);

  const wingBriefing = wing.locator('#briefing[data-state="open"]');
  await wingBriefing.locator("#crew-code").fill(code ?? "");
  await wingBriefing.locator('button[data-action="join-crew"]').click();
  await expect(wingBriefing.locator(".crew")).toHaveAttribute("data-members", "2");
  await expect(hostBriefing.locator(".crew")).toHaveAttribute("data-members", "2");
  await expect(hostBriefing.locator(".crew")).toContainText(wingName);
  await expect(wingBriefing.locator(".crew")).toContainText(`${hostName} (host)`);

  // The wing cannot launch on their own; the host can.
  await expect(wingBriefing.locator('button[data-tier="1"]')).toBeDisabled();
  await hostBriefing.locator('button[data-tier="1"]').click();
  await page.locator("#hud[data-x]").waitFor();

  const follow = wingBriefing.locator('button[data-action="follow"]');
  await expect(follow).toBeEnabled();
  await follow.click();
  await wing.locator("#hud[data-x]").waitFor();
  // Both ships are in the one instance.
  await expect
    .poll(async () => (await wing.locator("#hud").getAttribute("data-ships"))?.split(" ").length)
    .toBeGreaterThanOrEqual(2);
  await leave(wing);
});
