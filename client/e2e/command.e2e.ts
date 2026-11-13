import { Vector3 } from "three";
import { ZOOM_MAX } from "../src/view/look";
import { worldToScreen } from "../src/view/picking";
import { aimRig, createRig } from "../src/view/view";
import {
  addAuthenticator,
  expect,
  flyClear,
  leave,
  position,
  QUIET,
  RICH,
  signUp,
  test,
} from "./fixtures";

test("launch facts stay beside the action and preparation controls keep usable hit areas", async ({
  page,
}) => {
  await signUp(page);
  for (const size of [
    { width: 1280, height: 720 },
    { width: 1920, height: 1080 },
  ]) {
    await page.setViewportSize(size);
    const launch = page.locator('#briefing [data-tier="1"]');
    const stake = page.locator("#briefing .stake");
    await expect(stake).toContainText("Flies the basic fitting");
    await expect(stake).toContainText("Supplies: 40 cr");
    const facts = await stake.boundingBox();
    const action = await launch.boundingBox();
    if (!facts || !action) throw new Error("Launch facts and action must be drawn");
    expect(facts.y).toBeGreaterThanOrEqual(44);
    expect(facts.y + facts.height).toBeLessThanOrEqual(action.y);
    expect(action.y + action.height).toBeLessThanOrEqual(size.height - 30);
    const small = await page
      .locator(
        "#home button:visible, #briefing button:visible, #briefing select:visible, #crew-code:visible",
      )
      .evaluateAll((nodes) =>
        nodes
          .filter((node) => {
            const box = node.getBoundingClientRect();
            return box.width < 43.9 || box.height < 43.9;
          })
          .map((node) => node.outerHTML),
      );
    expect(small).toEqual([]);
  }
});

test("map keys navigate real destinations while safety instruments remain in view", async ({
  page,
}) => {
  await signUp(page, QUIET);
  await page.locator('#briefing [data-tier="1"]').click();
  await page.locator("#hud[data-x]").waitFor();
  // Off the berth, so the engage button is live: a jump is made from open space.
  await flyClear(page);
  await page.keyboard.press("KeyM");
  await expect(page.locator('.chart-row[data-target="dock"]')).toBeFocused();
  await page.keyboard.press("ArrowDown");
  await expect(page.locator('.chart-row[data-target="oreField"]')).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page.locator("#jump")).toHaveAttribute("data-target", "oreField");
  for (const size of [
    { width: 1280, height: 720 },
    { width: 1920, height: 1080 },
  ]) {
    await page.setViewportSize(size);
    await expect(async () => {
      const chart = await page.locator(".chart-window").boundingBox();
      const gauges = await page.locator("#ship").boundingBox();
      if (!chart || !gauges) throw new Error("Map and ship readings must be visible");
      expect(chart.y + chart.height).toBeLessThanOrEqual(gauges.y);
      const measured = await page.locator(".chart-svg-full").evaluate((node) => {
        const matrix = (node as SVGSVGElement).getScreenCTM();
        const labels = [...node.querySelectorAll("text")].filter(
          (label) => getComputedStyle(label).display !== "none",
        );
        const hits = [...node.querySelectorAll(".place-hit")];
        return {
          text: labels.map(
            (label) => parseFloat(getComputedStyle(label).fontSize) * (matrix?.a ?? 0),
          ),
          hits: hits.map((hit) => {
            const b = hit.getBoundingClientRect();
            return Math.min(b.width, b.height);
          }),
        };
      });
      expect(Math.min(...measured.text)).toBeGreaterThanOrEqual(10.99);
      expect(Math.min(...measured.hits)).toBeGreaterThanOrEqual(43.99);
    }).toPass({ timeout: 5_000 });
  }
  await page.keyboard.press("F6");
  await expect(page.locator('[data-action="engage-jump"]')).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page.locator("#jump")).toHaveAttribute("data-state", "spooling");
});

test("a saved unavailable rig stays a plan and exposes its facility prerequisite", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await signUp(page);
  await page.locator('[data-action="view:hangar"]').click();
  const scout = page.locator('[data-fitting="scout_rig"]');
  await expect(scout).toContainText("Sensor array tier 1 powered");
  await scout.locator('[data-action="fit:scout_rig"]').click();
  await expect(scout).toHaveAttribute("data-saved", "true");
  await expect(scout).toContainText("Saved · stays home");
  await expect(page.locator("#briefing .stake")).toContainText("Flies the basic fitting");
  for (const id of ["deep_scan", "heat_sink"]) {
    const module = page.locator(`[data-fitting="${id}"]`);
    await module.locator(`[data-action="fit:${id}"]`).click();
    await expect(module).toHaveAttribute("data-saved", "true");
  }
  const stake = page.locator("#briefing .stake");
  await expect(stake).toContainText("Scout rig stays home");
  await expect(stake).toContainText("Deep-scan array stays home");
  await expect(stake).toContainText("Heat sink stays home");
  for (const size of [
    { width: 1280, height: 720 },
    { width: 1920, height: 1080 },
  ]) {
    await page.setViewportSize(size);
    const facts = await stake.boundingBox();
    const action = await page.locator('#briefing [data-tier="1"]').boundingBox();
    if (!facts || !action) throw new Error("Recovery plan and launch action must be drawn");
    expect(facts.y).toBeGreaterThanOrEqual(44);
    expect(facts.y + facts.height).toBeLessThanOrEqual(action.y);
    expect(action.y + action.height).toBeLessThanOrEqual(size.height - 30);
  }
});

test("crew edits explain invalid totals and become applied only after acknowledgement", async ({
  page,
}) => {
  test.setTimeout(120_000);
  await signUp(page, RICH);
  await page.locator('[data-action="create-crew"]').click();
  const code = await page.locator(".crew[data-code]").getAttribute("data-code");
  const wing = await page.context().newPage();
  await addAuthenticator(wing);
  try {
    await signUp(wing, RICH);
    await wing.locator("#crew-code").fill(code ?? "");
    await wing.locator('[data-action="join-crew"]').click();
    await expect(page.locator(".crew")).toHaveAttribute("data-members", "2");
    await page.locator('[data-split="0"]').fill("60");
    await expect(page.locator(".crew-validation")).toContainText("110 / 100%");
    await expect(page.locator('[data-action="set-split"]')).toBeDisabled();
    // An account redraw must not turn an unsaved edit into an applied agreement.
    await page.locator('[data-action="view:market"]').click();
    await page.locator('[data-action="buy:ore"]').click();
    await expect(page.locator('[data-split="0"]')).toHaveValue("60");
    await page.locator('[data-split="1"]').fill("40");
    await page.locator('[data-action="set-split"]').click();
    await expect(page.locator(".crew-validation")).toContainText("Applied");
    await expect(wing.locator(".crew")).toContainText("60%");
    await expect(wing.locator(".crew")).toContainText("40%");
  } finally {
    await leave(wing);
  }
});

test("crew sees a spool and the jumping pilot regains the camera from a selected crewmate", async ({
  page,
}) => {
  test.setTimeout(150_000);
  await page.setViewportSize({ width: 1280, height: 720 });
  await signUp(page, QUIET);
  await page.locator('[data-action="create-crew"]').click();
  const code = await page.locator(".crew[data-code]").getAttribute("data-code");
  const wing = await page.context().newPage();
  await addAuthenticator(wing);
  try {
    await signUp(wing, QUIET);
    await wing.locator("#crew-code").fill(code ?? "");
    await wing.locator('[data-action="join-crew"]').click();
    await expect(page.locator(".crew")).toHaveAttribute("data-members", "2");
    await page.locator('#briefing [data-tier="1"]').click();
    await page.locator("#hud[data-x]").waitFor();
    await wing.locator('[data-action="follow"]').click();
    await wing.locator("#hud[data-x]").waitFor();
    await expect(page.locator("#hud")).toHaveAttribute("data-camera-moving", "false");
    const own = await page.locator("#hud").getAttribute("data-ship");
    const other = await wing.locator("#hud").getAttribute("data-ship");
    // A jump is made from open space: the pilot flies off the berth, then selects the crewmate
    // still on theirs, found on screen with the client's own camera.
    await flyClear(page);
    const crewmate = await position(wing);
    // Zoomed fully out, so the berth behind is in view.
    await page.mouse.move(640, 360);
    for (let i = 0; i < 8; i += 1) await page.mouse.wheel(0, 400);
    await expect(async () => {
      const own = await position(page);
      const rig = createRig(1280 / 720, ZOOM_MAX, "ortho");
      aimRig(rig, new Vector3(own.x, 0, own.z), 33);
      const at = worldToScreen(new Vector3(crewmate.x, 0, crewmate.z), rig.camera, {
        width: 1280,
        height: 720,
      });
      await page.mouse.click(at.x, at.y);
      await expect(page.locator("#hud")).toHaveAttribute("data-camera", other ?? "", {
        timeout: 500,
      });
    }).toPass({ timeout: 10_000 });
    await page.keyboard.press("KeyM");
    await page.keyboard.press("Digit2");
    await page.keyboard.press("KeyJ");
    await expect(wing.locator("#hud")).toHaveAttribute("data-crew-spools", "1");
    await expect(page.locator("#hud")).toHaveAttribute("data-jumps", "1", { timeout: 20_000 });
    await expect(page.locator("#hud")).toHaveAttribute("data-camera", own ?? "");
    await expect(wing.locator("#hud")).toHaveAttribute("data-crew-spools", "0");
  } finally {
    await leave(wing);
  }
});
