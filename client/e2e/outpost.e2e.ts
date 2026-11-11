import type { Page } from "@playwright/test";
import { RICH, expect, signUp, test } from "./fixtures";

interface SceneReport {
  sockets: string[];
  facilities: string[];
  installed: { socket: string; facility: string; tier: number; powered: boolean }[];
}

/** What the outpost scene holds, as the page reports it. */
async function scene(page: Page): Promise<SceneReport> {
  const json = await page.locator("#home[data-scene]").getAttribute("data-scene");
  return JSON.parse(json ?? "{}") as SceneReport;
}

const home = (page: Page) => page.locator('#home[data-state="open"]');
const account = (page: Page) => page.locator("#account");

test("the outpost loads the station, both sockets and every module", async ({ page }) => {
  await signUp(page);
  await expect(home(page)).toHaveAttribute("data-scene", /sockets/);
  const report = await scene(page);
  expect(report.sockets).toEqual(["a", "b"]);
  expect(report.facilities).toEqual([
    "fabrication_bay:1",
    "fabrication_bay:2",
    "refinery:1",
    "refinery:2",
    "sensor_array:1",
    "sensor_array:2",
  ]);
  expect(report.installed).toEqual([]);
  // A new pilot cannot afford a facility, and socket B is closed.
  await expect(home(page).locator('button[data-action="install:refinery"]')).toBeDisabled();
  await expect(home(page).locator('button[data-action="socket:b"]')).toHaveAttribute(
    "data-state",
    "closed",
  );
});

test("the run docks at the outpost's own station, the one the home shows", async ({ page }) => {
  await signUp(page);
  await expect(page.locator("body")).toHaveAttribute("data-station", "home");
  await page.locator('#briefing[data-state="open"] button[data-tier="1"]').click();
  await page.locator("#hud[data-x]").waitFor();
  await expect(page.locator("body")).toHaveAttribute("data-station", "dock");
});

test("building a refinery puts it on its socket and opens its pocket", async ({ page }) => {
  await signUp(page, RICH);
  await expect(account(page)).toHaveAttribute("data-credits", "1100");
  const build = home(page).locator('button[data-action="install:refinery"]');
  await expect(build.locator("xpath=..")).toContainText("Opens: Volatile veins pocket");
  await build.click();
  await expect(account(page)).toHaveAttribute("data-credits", "950");
  await expect(account(page)).toHaveAttribute("data-ore", "30");
  await expect(home(page)).toHaveAttribute("data-installed", "a:refinery:1 b:-:0");
  await expect
    .poll(async () => (await scene(page)).installed)
    .toEqual([{ socket: "a", facility: "refinery", tier: 1, powered: true }]);
  // The contract is shown again, with the pocket the refinery opens.
  await expect(page.locator('#briefing[data-state="open"]')).toContainText("Volatile veins: open");
  await expect(home(page).locator('button[data-action="socket:b"]')).toHaveAttribute(
    "data-state",
    "empty",
  );
});

test("reconfiguring moves a facility for the reinstallation cost", async ({ page }) => {
  await signUp(page, RICH);
  await home(page).locator('button[data-action="install:sensor_array"]').click();
  await expect(home(page)).toHaveAttribute("data-installed", "a:sensor_array:1 b:-:0");
  await home(page).locator('button[data-action="remove"]').click();
  await expect(home(page)).toHaveAttribute("data-installed", "a:-:0 b:-:0");
  await expect(account(page)).toHaveAttribute("data-credits", "950");
  await home(page).locator('button[data-action="socket:b"]').click();
  await expect(home(page)).toHaveAttribute("data-selected", "b");
  const again = home(page).locator('button[data-action="install:sensor_array"]');
  await expect(again).toHaveText("Install the Sensor array again");
  await again.click();
  await expect(home(page)).toHaveAttribute("data-installed", "a:-:0 b:sensor_array:1");
  await expect(account(page)).toHaveAttribute("data-credits", "910");
  await expect(account(page)).toHaveAttribute("data-alloy", "24");
  await expect
    .poll(async () => (await scene(page)).installed)
    .toEqual([{ socket: "b", facility: "sensor_array", tier: 1, powered: true }]);
});

test("the contract shows the stake at replacement price", async ({ page }) => {
  await signUp(page, RICH);
  const stake = page.locator('#briefing[data-state="open"] .stake');
  await expect(stake).toHaveAttribute("data-stake", "0");
  await expect(stake).toContainText("Flies the basic fitting · hold 20");
  // The refinery's extraction rig comes with it and is fitted at once.
  await home(page).locator('button[data-action="install:refinery"]').click();
  await expect(stake).toHaveAttribute("data-stake", "60");
  await home(page).locator('button[data-action="buy-fitting:cargo_frame"]').click();
  await expect(home(page).locator('[data-fitting="cargo_frame"]')).toHaveAttribute(
    "data-owned",
    "1",
  );
  await home(page).locator('button[data-action="fit:cargo_frame"]').click();
  await expect(stake).toHaveAttribute("data-stake", "90");
  await expect(stake).toContainText("Flies: Extraction rig, Cargo frame · hold 38");
  await expect(stake).toContainText("Stake: 90 cr of rigs and modules at replacement price");
});
