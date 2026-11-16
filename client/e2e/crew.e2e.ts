import { QUIET, addAuthenticator, expect, leave, signUp, test } from "./fixtures";

test("two pilots crew up by code, and the wing follows the host into one run", async ({ page }) => {
  test.setTimeout(90_000);
  // Each pilot signs up before the next page opens, as two people at two machines would.
  const hostName = await signUp(page, QUIET);
  const wing = await page.context().newPage();
  await addAuthenticator(wing);
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

test("four ships leave the hangar mouth one after another and are handed over on the lane", async ({
  page,
}) => {
  test.setTimeout(180_000);
  await page.setViewportSize({ width: 1600, height: 1000 });
  const pages = [page];
  try {
    // Each pilot signs up before the next page opens, as four people at four machines would.
    await signUp(page, QUIET);
    for (let i = 1; i < 4; i += 1) {
      const wing = await page.context().newPage();
      await addAuthenticator(wing);
      pages.push(wing);
      await signUp(wing, QUIET);
    }
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
      // Ships wait in the hangar, not on berths: the footer counts the crew.
      await expect(pilot.locator("#utility")).toContainText("HOME OUTPOST / CREW 4 OF 4");
      // Each pilot is told the moment the launch hands the ship over, and where it is then.
      await pilot.evaluate(() => {
        const run = document.querySelector<HTMLElement>("#run");
        const hud = document.querySelector<HTMLElement>("#hud");
        if (!run || !hud) throw new Error("no run status");
        new MutationObserver((_, observer) => {
          if (run.dataset.phase !== "approach") return;
          observer.disconnect();
          Object.assign(window, {
            undocked: { at: Date.now(), x: Number(hud.dataset.x), z: Number(hud.dataset.z) },
          });
        }).observe(run, { attributes: true });
      });
    }
    await page.screenshot({ path: test.info().outputPath("four-pilot-hangar-hud.png") });
    await briefing(page).locator('[data-tier="1"]').click();
    await page.locator("#hud[data-x]").waitFor();
    // Followers launch out of roster order; each queues behind the ships before it.
    for (const index of [3, 1, 2]) {
      const pilot = pages[index];
      if (!pilot) throw new Error("Missing test pilot");
      await briefing(pilot).locator('[data-action="follow"]').click();
      await pilot.locator("#hud[data-x]").waitFor();
    }
    await expect
      .poll(async () => (await page.locator("#hud").getAttribute("data-ships"))?.split(" ").length)
      .toBe(4);
    const handovers = await Promise.all(
      pages.map(async (pilot) => {
        await pilot.waitForFunction(() => "undocked" in window, undefined, { timeout: 30_000 });
        return pilot.evaluate(
          () => (window as unknown as { undocked: { at: number; x: number; z: number } }).undocked,
        );
      }),
    );
    // Each is handed over at the lane's end, 44 m out of the station at (-120, 0) along +x, on
    // the lane's axis: none was put down beside another.
    for (const handover of handovers) {
      // A frame either side of the moment: the ship moves 8 m/s, drawn at up to 1 m from where
      // the last snapshot put it.
      expect(handover.x).toBeGreaterThan(-78);
      expect(handover.x).toBeLessThan(-66);
      expect(Math.abs(handover.z)).toBeLessThan(0.5);
    }
    // One after another: a departing ship moves only once the one ahead is 10 m further along
    // the lane, 1.25 s at the launch's 8 m/s. (The order among ships launched together is the
    // roster's; the server's crew test proves it.)
    const times = handovers.map((handover) => handover.at).sort((a, b) => a - b);
    for (let i = 1; i < times.length; i += 1) {
      expect((times[i] ?? 0) - (times[i - 1] ?? 0)).toBeGreaterThan(1_000);
    }
  } finally {
    for (const wing of pages.slice(1)) await leave(wing);
  }
});
