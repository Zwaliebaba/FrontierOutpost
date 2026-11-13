import { describe, expect, it } from "vitest";
import { DEEP_SCAN, SCAN_REFUSAL_TEXT, scanReach } from "./scan";
import type { ScanReachInput } from "./scan";

// The compact site: its centre, and a scan range that covers it.
const at: ScanReachInput = {
  phase: "objective",
  x: 70,
  z: 0,
  centreX: 70,
  centreZ: 0,
  range: 180,
  fitted: [],
};

describe("the scanner's reach", () => {
  it("is the scan range around the site's centre, whatever the leg", () => {
    expect(scanReach(at)).toEqual({ inReach: true, short: 0 });
    expect(scanReach({ ...at, x: -110 })).toEqual({ inReach: true, short: 0 });
    expect(scanReach({ ...at, x: -110, phase: "approach" })).toEqual({ inReach: true, short: 0 });
    expect(scanReach({ ...at, x: -120, phase: "atDock" })).toEqual({ inReach: false, short: 10 });
  });

  it("says how far there is to close when out of range", () => {
    // Ion fog shortens the range to less than the site.
    const fog = { ...at, range: 108 };
    const reach = scanReach({ ...fog, x: 70 + 90, z: 120 });
    expect(reach?.inReach).toBe(false);
    expect(reach?.short).toBeCloseTo(42);
  });

  it("is everywhere off the dock with the deep-scan array", () => {
    const deep = { ...at, x: -80, fitted: ["scout_rig", DEEP_SCAN] };
    expect(scanReach({ ...deep, phase: "approach" })).toEqual({ inReach: true, short: 0 });
    expect(scanReach({ ...deep, phase: "return" })).toEqual({ inReach: true, short: 0 });
    expect(scanReach({ ...deep, x: -120, phase: "atDock" })).toEqual({ inReach: false, short: 0 });
  });

  it("is nothing once the run is over", () => {
    for (const phase of ["docked", "destroyed", "abandoned"] as const) {
      expect(scanReach({ ...at, phase })).toBeNull();
    }
  });
});

describe("a refused scan", () => {
  it("says what to do about it", () => {
    expect(SCAN_REFUSAL_TEXT.moving).toContain("hold still");
    expect(SCAN_REFUSAL_TEXT.outOfRange).toContain("move closer");
    expect(SCAN_REFUSAL_TEXT.scanning).toContain("already");
    expect(SCAN_REFUSAL_TEXT.revealed).toContain("already");
  });

  it("names the scan in every reason, as the extractor's and the drive's notices do", () => {
    for (const text of Object.values(SCAN_REFUSAL_TEXT)) expect(text).toMatch(/^Scan: /);
  });
});
