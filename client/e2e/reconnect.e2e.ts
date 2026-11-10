import { QUIET, addAuthenticator, expect, launch, test } from "./fixtures";

// On the quiet server, so nothing destroys the pilot while the tests come and go.

test("a reload mid-run rejoins the same run", async ({ page }) => {
  await launch(page, QUIET);
  const hud = page.locator("#hud");
  const ship = await hud.getAttribute("data-ship");
  // Out past the undock radius: about 30 m.
  await page.keyboard.down("KeyW");
  await page.waitForTimeout(5_000);
  await page.keyboard.up("KeyW");
  await expect(page.locator("#run")).toHaveAttribute("data-phase", "approach");

  // The connection goes with the page; the run waits, and the token signs straight back in.
  await page.reload();
  await expect(page.locator("#run")).toHaveAttribute("data-phase", "approach");
  await expect(hud).toHaveAttribute("data-ship", ship ?? "");
  await expect(page.locator("#briefing")).not.toHaveAttribute("data-state", "open");
});

test("signing in on another page takes the run over, and the first page stays out", async ({
  page,
  browser,
}) => {
  await launch(page, QUIET);
  const token = await page.evaluate(() => sessionStorage.getItem("frontier.session"));

  const other = await browser.newPage();
  await addAuthenticator(other);
  await other.goto(QUIET);
  // Only once this first load has found no token and asks to sign in: a token set while it is
  // still starting would be resumed by it, and the reload below would cut that short.
  await expect(other.locator("#signin")).toHaveAttribute("data-state", "open");
  await other.evaluate((value) => {
    sessionStorage.setItem("frontier.session", value ?? "");
  }, token);
  await other.reload();
  // What the page shows, so a failure says whether it got the run, a contract or neither.
  const state = () =>
    other.evaluate(() => ({
      phase: document.querySelector<HTMLElement>("#run")?.dataset.phase ?? null,
      ship: document.querySelector<HTMLElement>("#hud")?.dataset.ship ?? null,
      briefing: document.querySelector<HTMLElement>("#briefing")?.dataset.state ?? null,
      status: document.querySelector("#status")?.textContent ?? null,
      notice: document.querySelector("#notice")?.textContent ?? null,
    }));
  await expect
    .poll(state, { message: "the second page rejoins the run at the dock", timeout: 10_000 })
    .toMatchObject({ phase: "atDock" });

  const status = page.locator("#status");
  await expect(status).toContainText("Signed in on another page");
  // It does not reconnect and take the run back.
  await page.waitForTimeout(3_000);
  await expect(status).toContainText("Signed in on another page");
  await expect(other.locator("#run")).toHaveAttribute("data-phase", "atDock");
  await other.close();
});
