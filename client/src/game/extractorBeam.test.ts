import { describe, expect, it } from "vitest";
import { Color, Vector3 } from "three";
import { ExtractorBeam, FRAGMENTS, FRAGMENT_SPEED, fragmentAt } from "./extractorBeam";

describe("fragmentAt", () => {
  it("carries each fragment from the deposit toward the hull as time passes", () => {
    const before = fragmentAt(0, FRAGMENTS, 0.1, 5);
    const after = fragmentAt(0, FRAGMENTS, 0.3, 5);
    expect(after.along - before.along).toBeCloseTo((0.2 * FRAGMENT_SPEED) / 5, 5);
  });

  it("spreads the fragments evenly along the beam", () => {
    const alongs = Array.from(
      { length: FRAGMENTS },
      (_, i) => fragmentAt(i, FRAGMENTS, 0, 5).along,
    ).sort((a, b) => a - b);
    alongs.forEach((along, i) => {
      expect(along).toBeCloseTo(i / FRAGMENTS, 5);
    });
  });

  it("breaks out of the crystal small, flies full size, and shrinks into the hull", () => {
    const at = (along: number) => fragmentAt(0, 1, (along * 5) / FRAGMENT_SPEED, 5);
    expect(at(0).scale).toBeCloseTo(0);
    expect(at(0.5).scale).toBe(1);
    expect(at(0.95).scale).toBeLessThan(0.3);
    // On the beam at both ends, bowed out between.
    expect(Math.abs(at(0).side)).toBeLessThan(1e-6);
    expect(Math.abs(at(0.5).side)).toBeGreaterThan(0.1);
  });
});

describe("ExtractorBeam", () => {
  const start = new Vector3(0, -0.5, 0);
  const end = new Vector3(4, 0.5, 0);

  it("puts the fragments between the deposit and the ship while it works", () => {
    const beam = new ExtractorBeam();
    beam.aim(start, end, new Color(0.6, 0.5, 0.3));
    beam.step(0.4, false);
    expect(beam.fragments.visible).toBe(true);
    for (const chunk of beam.fragments.children) {
      expect(chunk.position.x).toBeGreaterThanOrEqual(-1e-6);
      expect(chunk.position.x).toBeLessThanOrEqual(4 + 1e-6);
    }
  });

  it("holds still and shows no fragments when the system asks for reduced motion", () => {
    const beam = new ExtractorBeam();
    beam.aim(start, end, new Color(0.6, 0.5, 0.3));
    beam.step(0.4, true);
    expect(beam.beam.visible).toBe(true);
    expect(beam.fragments.visible).toBe(false);
  });

  it("takes everything away when hidden", () => {
    const beam = new ExtractorBeam();
    beam.aim(start, end, new Color(0.6, 0.5, 0.3));
    beam.step(0.1, false);
    beam.hide();
    beam.step(0.1, false);
    expect(beam.beam.visible).toBe(false);
    expect(beam.fragments.visible).toBe(false);
  });
});
