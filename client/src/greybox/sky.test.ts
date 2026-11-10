import { describe, expect, it } from "vitest";
import { createStarField } from "./sky";

describe("greybox sky", () => {
  const field = createStarField(11, 4000);
  const star = (index: number) => [...field.directions.slice(index * 3, index * 3 + 3)];

  it("replays from its seed", () => {
    expect(createStarField(11, 4000).directions).toEqual(field.directions);
    expect(createStarField(12, 4000).directions).not.toEqual(field.directions);
  });

  it("places every star on the unit sphere", () => {
    for (let i = 0; i < 4000; i += 1) {
      expect(Math.hypot(...star(i))).toBeCloseTo(1, 5);
    }
  });

  it("covers the whole sky, so every camera direction shows stars", () => {
    const octants = new Set<string>();
    for (let i = 0; i < 4000; i += 1) {
      octants.add(
        star(i)
          .map((v) => (v > 0 ? "+" : "-"))
          .join(""),
      );
    }
    expect(octants.size).toBe(8);
  });

  it("gathers a band of stars, denser than the rest of the sky", () => {
    // The band's pole is the y axis tilted by 0.9 rad about x.
    const pole = [0, Math.cos(0.9), Math.sin(0.9)];
    let nearBand = 0;
    for (let i = 0; i < 4000; i += 1) {
      const [x = 0, y = 0, z = 0] = star(i);
      if (Math.abs(x * (pole[0] ?? 0) + y * (pole[1] ?? 0) + z * (pole[2] ?? 0)) < 0.15)
        nearBand += 1;
    }
    // A uniform sky would put 15% of stars this close to any great circle.
    expect(nearBand / 4000).toBeGreaterThan(0.35);
  });

  it("makes most stars dim and a few bright", () => {
    const brightness = Array.from({ length: 4000 }, (_, i) => field.colours[i * 3 + 1] ?? 0);
    const bright = brightness.filter((value) => value > 0.5).length;
    expect(Math.min(...brightness)).toBeGreaterThan(0.05);
    expect(bright).toBeGreaterThan(40);
    expect(bright).toBeLessThan(800);
  });
});
