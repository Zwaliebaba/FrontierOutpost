import { describe, expect, it } from "vitest";
import { createRng } from "../view/rng";
import { chooseRangePair, headingErrorDeg, median, planRun, summarise } from "./trials";
import type { TrialRecord } from "./trials";

const record = (
  kind: TrialRecord["kind"],
  outcome: TrialRecord["outcome"],
  ms = 800,
  value: number | null = null,
): TrialRecord => ({
  kind,
  outcome,
  ms,
  value,
  occluded: false,
  cameraYawDeg: 0,
  cameraPitchDeg: 33,
  viewWidth: 40,
});

describe("greybox trials", () => {
  it("measures heading error on the plane, in either direction", () => {
    expect(headingErrorDeg(0, 2, 2, 7, 2)).toBeCloseTo(0);
    expect(headingErrorDeg(0, 2, 2, 2, 7)).toBeCloseTo(90);
    expect(headingErrorDeg(0, 2, 2, 2, -3)).toBeCloseTo(90);
    expect(headingErrorDeg(Math.PI, 0, 0, 5, 0)).toBeCloseTo(180);
    expect(headingErrorDeg(-Math.PI + 0.1, 0, 0, -5, 0.5)).toBeLessThan(12);
  });

  it("chooses the pair whose range ratio is closest to the target", () => {
    const pair = chooseRangePair({ x: 0, z: 0 }, [
      { id: "a", x: 10, z: 0 },
      { id: "b", x: 0, z: 12 },
      { id: "c", x: -25, z: 0 },
    ]);
    expect(pair?.near.id).toBe("a");
    expect(pair?.far.id).toBe("b");
    expect(pair?.ratio).toBeCloseTo(1.2);
  });

  it("needs two candidates away from the origin", () => {
    expect(chooseRangePair({ x: 0, z: 0 }, [{ id: "a", x: 10, z: 0 }])).toBeNull();
    expect(
      chooseRangePair({ x: 0, z: 0 }, [
        { id: "a", x: 0.1, z: 0 },
        { id: "b", x: 9, z: 0 },
      ]),
    ).toBeNull();
  });

  it("plans a shuffled run with equal counts that replays from its seed", () => {
    const plan = planRun(4, createRng(5));
    expect(plan).toHaveLength(12);
    for (const kind of ["acquire", "heading", "range"]) {
      expect(plan.filter((entry) => entry === kind)).toHaveLength(4);
    }
    expect(planRun(4, createRng(5))).toEqual(plan);
    expect(planRun(4, createRng(6))).not.toEqual(plan);
  });

  it("takes the median of odd, even and empty lists", () => {
    expect(median([5, 1, 3])).toBe(3);
    expect(median([4, 1, 3, 2])).toBe(2.5);
    expect(median([])).toBeNull();
  });

  it("summarises a run", () => {
    const summary = summarise([
      record("acquire", "correct", 600),
      record("acquire", "correct", 1000),
      record("acquire", "wrong", 400),
      record("acquire", "miss", 300),
      record("heading", "correct", 900, 10),
      record("heading", "wrong", 900, 40),
      record("range", "correct", 700, 1.2),
      record("range", "correct", 700, 1.4),
      record("range", "timeout", 6000, 1.3),
    ]);
    expect(summary.trials).toBe(9);
    // Slow or wrong answers must not flatter the selection time.
    expect(summary.acquireMedianMs).toBe(800);
    expect(summary.acquireErrorRate).toBe(0.5);
    expect(summary.headingCorrectRate).toBe(0.5);
    expect(summary.rangeCorrectRate).toBeCloseTo(2 / 3);
    expect(summary.rangeMeanRatio).toBeCloseTo(1.3);
  });

  it("reports no data for kinds that were not run", () => {
    const summary = summarise([record("heading", "correct", 500, 5)]);
    expect(summary.acquireMedianMs).toBeNull();
    expect(summary.acquireErrorRate).toBeNull();
    expect(summary.rangeCorrectRate).toBeNull();
    expect(summary.rangeMeanRatio).toBeNull();
  });
});
