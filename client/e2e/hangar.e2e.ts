import {
  QUIET,
  expect,
  flyBackIntoTheMouth,
  leaveHangarAndStop,
  phasesSeen,
  position,
  signUp,
  test,
  watchPhases,
} from "./fixtures";

// Plan 9.6's proof: launch and dock, through the station's one hangar mouth. On the quiet server,
// so nothing meets the ship on the lane.

test("a launch leaves through the hangar mouth, and flying back into it docks", async ({
  page,
}) => {
  test.setTimeout(90_000);
  await signUp(page, QUIET);
  await watchPhases(page);
  await page.locator('#briefing[data-state="open"] button[data-tier="1"]').click();
  await page.locator("#hud[data-x]").waitFor();
  const ship = (await page.locator("#hud").getAttribute("data-ship")) ?? "";
  // The launch flies the ship: the one prompt says so, and the camera follows it out.
  await expect(page.locator('#prompts [data-prompt="departing"]')).toContainText(
    "Leaving the hangar",
  );
  await expect(page.locator("#hud")).toHaveAttribute("data-camera", ship);
  // Out of the compact site's station at (-120, 0), facing +x: handed over at the lane's end.
  await leaveHangarAndStop(page);
  const out = await position(page);
  expect(out.x).toBeGreaterThan(-77);
  expect(Math.abs(out.z)).toBeLessThan(0.5);
  await expect(page.locator('#prompts [data-prompt="departing"]')).toHaveCount(0);

  await flyBackIntoTheMouth(page);
  await expect(page.locator("#summary")).toHaveAttribute("data-ending", "docked");
  const seen = await phasesSeen(page);
  // Flown out by the launch, flown by the pilot, then caught at the mouth and drawn in.
  const at = (phase: string) => seen.phases.indexOf(phase);
  expect(at("atDock")).toBe(0);
  expect(at("approach")).toBeGreaterThan(at("atDock"));
  expect(at("docking")).toBeGreaterThan(at("approach"));
  // Behind the mouth, waiting to leave or drawn in, the ship was not drawn.
  expect(seen.hangar.some((ids) => ids.split(" ").includes(ship))).toBe(true);
});
