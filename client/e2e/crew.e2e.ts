import { QUIET, addAuthenticator, expect, leave, position, signUp, test } from "./fixtures";

test("two pilots crew up by code, and the wing follows the host into one run", async ({ page }) => {
  test.setTimeout(90_000);
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

test("four ships park at distinct berths and follow in roster order even when launch order differs", async ({
  page,
}) => {
  test.setTimeout(180_000);
  await page.setViewportSize({ width: 1600, height: 1000 });
  const pages = [page];
  for (let i = 1; i < 4; i += 1) {
    const wing = await page.context().newPage();
    await addAuthenticator(wing);
    pages.push(wing);
  }
  try {
    for (const pilot of pages) await signUp(pilot, QUIET);
    const briefing = (pilot: typeof page) => pilot.locator('#briefing[data-state="open"]');
    await briefing(page).locator('[data-action="create-crew"]').click();
    const code = await briefing(page).locator(".crew").getAttribute("data-code");
    for (const pilot of pages.slice(1)) {
      await briefing(pilot)
        .locator("#crew-code")
        .fill(code ?? "");
      await briefing(pilot).locator('[data-action="join-crew"]').click();
    }
    for (const pilot of pages) {
      await expect(briefing(pilot).locator(".crew")).toHaveAttribute("data-members", "4");
      await expect
        .poll(async () => {
          const report = JSON.parse(
            (await pilot.locator("#home").getAttribute("data-scene")) ?? "{}",
          ) as { berths?: unknown[] };
          return report.berths?.length;
        })
        .toBe(4);
    }
    await page.screenshot({ path: test.info().outputPath("four-berth-hud.png") });
    await briefing(page).locator('[data-tier="1"]').click();
    await page.locator("#hud[data-x]").waitFor();
    for (const index of [3, 1, 2]) {
      const pilot = pages[index];
      if (!pilot) throw new Error("Missing test pilot");
      await briefing(pilot).locator('[data-action="follow"]').click();
      await pilot.locator("#hud[data-x]").waitFor();
    }
    const positions = await Promise.all(pages.map(position));
    // Compact site's launch heading is +X. Authored lateral offsets map to -Z.
    for (let i = 0; i < 4; i += 1) {
      expect(positions[i]?.x).toBeCloseTo(-120, 0);
      expect(positions[i]?.z).toBeCloseTo(6 - i * 4, 0);
    }
  } finally {
    for (const wing of pages.slice(1)) await leave(wing);
  }
});
