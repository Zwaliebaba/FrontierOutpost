import { describe, expect, it } from "vitest";
import { targetReadout } from "./target";

describe("targetReadout", () => {
  it("names the target and says the weapons reach it", () => {
    expect(targetReadout("drone", 2, 20.6, 60, 255)).toEqual({
      name: "Drone 2",
      distance: "21 m",
      inRange: true,
      range: "In range",
      hullShare: 1,
      hull: "hull 100%",
    });
  });

  it("says from how near the weapons fire when it is out of range", () => {
    const far = targetReadout("core", 9, 140, 60, 128);
    expect(far.name).toBe("Nest core 9");
    expect(far.inRange).toBe(false);
    expect(far.range).toBe("Out of range · fires within 60 m");
    expect(far.hull).toBe("hull 50%");
    expect(far.hullShare).toBeCloseTo(0.502, 2);
  });
});
