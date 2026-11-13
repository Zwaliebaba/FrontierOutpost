import { describe, expect, it } from "vitest";
import { DUST_TILE, STREAK_MOST, createDust, placeDust, streakDust } from "./dust";

describe("flight dust", () => {
  const field = createDust(7);
  const placed = (x: number, z: number) => {
    const out = new Float32Array(field.homes.length);
    placeDust(field, x, z, out);
    return out;
  };

  it("replays from its seed", () => {
    expect(createDust(7).homes).toEqual(field.homes);
    expect(createDust(8).homes).not.toEqual(field.homes);
  });

  it("surrounds the camera wherever it is, however far it has flown", () => {
    for (const [x, z] of [
      [0, 0],
      [5_000.5, -12_345],
      [-3_210, 777],
    ] as const) {
      const out = placed(x, z);
      for (let i = 0; i < out.length; i += 3) {
        expect(Math.abs((out[i] ?? 0) - x)).toBeLessThanOrEqual(DUST_TILE / 2);
        expect(Math.abs((out[i + 2] ?? 0) - z)).toBeLessThanOrEqual(DUST_TILE / 2);
      }
    }
  });

  it("stays where it is in the world as the camera moves, so it slides past the ship", () => {
    const before = placed(100, 40);
    const after = placed(103, 41);
    let unmoved = 0;
    for (let i = 0; i < before.length; i += 3) {
      const same =
        Math.abs((before[i] ?? 0) - (after[i] ?? 0)) < 1e-3 &&
        Math.abs((before[i + 2] ?? 0) - (after[i + 2] ?? 0)) < 1e-3;
      if (same) unmoved += 1;
    }
    // Only specks crossing the tile's far edge wrap round; the rest keep their place.
    expect(unmoved / (before.length / 3)).toBeGreaterThan(0.95);
  });

  it("lies below the gameplay plane, so ships draw over it", () => {
    for (let i = 1; i < field.homes.length; i += 3) {
      expect(field.homes[i]).toBeLessThan(0);
    }
  });

  it("is sparse: a few dozen specks across the view at the starting zoom", () => {
    // 40 m across at the start; about as deep on the plane at the resting 33° and 16:9.
    const area = 40 * (40 / (16 / 9) / Math.sin((33 * Math.PI) / 180));
    const seen = (field.homes.length / 3) * (area / DUST_TILE ** 2);
    expect(seen).toBeGreaterThan(10);
    expect(seen).toBeLessThan(40);
  });

  describe("streaks", () => {
    const streaks = (vx: number, vz: number) => {
      const out = new Float32Array((field.homes.length / 3) * 6);
      streakDust(field, 10, 20, vx, vz, out);
      return out;
    };
    const length = (out: Float32Array, i: number) =>
      Math.hypot((out[i + 3] ?? 0) - (out[i] ?? 0), (out[i + 5] ?? 0) - (out[i + 2] ?? 0));

    it("vanish when the ship is still, so the dust is never taken for stars", () => {
      expect(length(streaks(0, 0), 0)).toBe(0);
    });

    it("grow with speed up to a cap, and point the way the ship is going", () => {
      const slow = streaks(5, 0);
      expect(length(slow, 0)).toBeCloseTo(1, 5);
      // The tail is ahead of the head in the direction of travel: the ship is passing it.
      expect((slow[3] ?? 0) - (slow[0] ?? 0)).toBeGreaterThan(0);
      expect(length(streaks(0, -400), 6)).toBeCloseTo(STREAK_MOST, 5);
    });

    it("start where the speck is drawn", () => {
      const heads = placed(10, 20);
      const out = streaks(3, 4);
      expect([out[6], out[7], out[8]]).toEqual([heads[3], heads[4], heads[5]]);
    });
  });
});
