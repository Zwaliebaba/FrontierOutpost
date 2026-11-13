import { describe, expect, it } from "vitest";
import {
  DUST_AT_REST,
  DUST_FULL_SPEED,
  DUST_TILE,
  createDust,
  dustOpacity,
  placeDust,
} from "./dust";

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

  it("draws specks of a few pixels, the deepest smallest and dimmest", () => {
    let deepest = 0;
    let nearest = 0;
    for (let i = 0; i < field.sizes.length; i += 1) {
      expect(field.sizes[i]).toBeGreaterThanOrEqual(2);
      expect(field.sizes[i]).toBeLessThanOrEqual(4);
      if ((field.homes[i * 3 + 1] ?? 0) < (field.homes[deepest * 3 + 1] ?? 0)) deepest = i;
      if ((field.homes[i * 3 + 1] ?? 0) > (field.homes[nearest * 3 + 1] ?? 0)) nearest = i;
    }
    expect(field.sizes[deepest]).toBeLessThan(field.sizes[nearest] ?? 0);
  });

  it("is faint: no speck as bright as a middling star", () => {
    for (const c of field.colours) expect(c).toBeLessThan(0.25);
  });

  describe("opacity", () => {
    it("fades nearly out when the ship is still, so the dust is not taken for stars", () => {
      expect(dustOpacity(0)).toBe(DUST_AT_REST);
      expect(DUST_AT_REST).toBeLessThan(0.5);
    });

    it("grows with speed to full, and stays full on the cruise drive", () => {
      expect(dustOpacity(DUST_FULL_SPEED / 2)).toBeGreaterThan(DUST_AT_REST);
      expect(dustOpacity(DUST_FULL_SPEED / 2)).toBeLessThan(1);
      expect(dustOpacity(DUST_FULL_SPEED)).toBe(1);
      expect(dustOpacity(33)).toBe(1);
    });
  });
});
