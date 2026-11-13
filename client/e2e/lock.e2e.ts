import { expect, flyUntilLocked, launch, test } from "./fixtures";
import type { Page } from "@playwright/test";

// Signal-color pixel assertions need native-density edges, including on the software runner.
test.use({ deviceScaleFactor: 1 });

test("Tab locks the nearest hostile and Escape releases it", async ({ page }) => {
  test.setTimeout(60_000);
  await launch(page);
  await flyUntilLocked(page);
  await page.keyboard.press("Escape");
  const lock = page.locator("#lock");

  await page.keyboard.press("Tab");
  await expect(lock).toHaveAttribute("data-target", /\d+/);
  await expect(lock).toContainText(/(Drone|Raider|Warden) \d+/);

  // Tab again moves to the next hostile in range, or keeps the only one (cycling order is
  // covered by selection.test.ts).
  await page.keyboard.press("Tab");
  await expect(lock).toHaveAttribute("data-target", /\d+/);

  await page.keyboard.press("Escape");
  await expect(lock).toBeEmpty();
});

/** Pixels near the hostile signal colour (sRGB) in a square at the centre of the view. */
async function bracketPixels(page: Page): Promise<number> {
  const size = 240;
  const viewport = page.viewportSize() ?? { width: 1280, height: 720 };
  // The DevTools protocol hands the image over as base64, with no Node Buffer involved.
  const cdp = await page.context().newCDPSession(page);
  const { data } = await cdp.send("Page.captureScreenshot", {
    format: "png",
    clip: {
      x: viewport.width / 2 - size / 2,
      y: viewport.height / 2 - size / 2,
      width: size,
      height: size,
      scale: 1,
    },
  });
  await cdp.detach();
  return page.evaluate(async (png) => {
    const image = new Image();
    image.src = `data:image/png;base64,${png}`;
    await image.decode();
    const canvas = document.createElement("canvas");
    canvas.width = image.width;
    canvas.height = image.height;
    const context = canvas.getContext("2d");
    if (!context) return -1;
    context.drawImage(image, 0, 0);
    const { data } = context.getImageData(0, 0, image.width, image.height);
    let count = 0;
    for (let i = 0; i < data.length; i += 4) {
      const [r = 0, g = 0, b = 0] = [data[i], data[i + 1], data[i + 2]];
      if (r > 240 && Math.abs(g - 170) < 12 && Math.abs(b - 66) < 20) count++;
    }
    return count;
  }, data);
}

test("the real renderer has a stencil for silhouettes and brackets the lock", async ({ page }) => {
  test.setTimeout(60_000);
  await launch(page);
  await flyUntilLocked(page);
  await page.keyboard.press("Escape");
  await page.waitForTimeout(300);
  // Silhouettes behind structures need a stencil buffer in the real renderer.
  const stencil = await page.evaluate(
    () => document.querySelector("canvas")?.getContext("webgl2")?.getContextAttributes()?.stencil,
  );
  expect(stencil).toBe(true);

  const before = await bracketPixels(page);
  await page.keyboard.press("Tab");
  await expect(page.locator("#lock")).toHaveAttribute("data-target", /\d+/);
  // Locking centres the view on the target; give the frame a moment to follow.
  await page.waitForTimeout(400);
  const after = await bracketPixels(page);
  expect(after).toBeGreaterThan(before + 40);
});
